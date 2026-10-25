#include "return_statistics.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace trading {
namespace {

void validate(std::span<const DailyResearchReturn> days) {
    if (days.empty() || days.size() > 512) { throw std::invalid_argument("return statistics require 1 to 512 days"); }
    std::string previous;
    for (const auto& day : days) {
        if (day.date.size() != 10 || day.date <= previous) {
            throw std::invalid_argument("research dates must be unique, ascending YYYY-MM-DD values");
        }
        static_cast<void>(parse_utc_seconds(day.date + "T00:00:00Z"));
        previous = day.date;
        if (!std::isfinite(day.window_gross_return) || !std::isfinite(day.window_net_return) ||
            day.window_gross_return <= -1 || day.window_net_return <= -1) {
            throw std::invalid_argument("research price returns must be finite and greater than -1");
        }
    }
}

} // namespace

std::vector<ReturnPathPoint> compound_return_path(std::span<const double> values) {
    if (values.empty()) { throw std::invalid_argument("return path requires observations"); }
    std::vector<ReturnPathPoint> result;
    result.reserve(values.size());
    double wealth = 1;
    double peak = 1;
    for (const double value : values) {
        if (!std::isfinite(value) || value <= -1) {
            throw std::invalid_argument("price returns must be finite and greater than -1");
        }
        wealth *= 1 + value;
        if (!std::isfinite(wealth) || wealth <= 0) {
            throw std::range_error("return aggregation exceeds the positive finite wealth range");
        }
        peak = std::max(peak, wealth);
        result.push_back({wealth, 1 - wealth / peak});
    }
    return result;
}

ReturnStatistics describe_return_series(std::span<const double> values) {
    const auto path = compound_return_path(values);
    double sum = 0;
    double drawdown = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (!std::isfinite(sum)) { throw std::overflow_error("return sum exceeds the numeric range"); }
        drawdown = std::max(drawdown, path[i].drawdown);
    }
    const bool constant = std::all_of(values.begin(), values.end(), [&](double value) { return value == values.front(); });
    const auto mean = constant ? values.front() : sum / static_cast<double>(values.size());
    ReturnStatistics result{mean, path.back().wealth - 1, drawdown, {}, {}};
    if (values.size() > 1) {
        double squared = 0;
        if (!constant) {
            for (const double value : values) { squared += (value - mean) * (value - mean); }
        }
        const auto deviation = std::sqrt(squared / static_cast<double>(values.size() - 1));
        if (!std::isfinite(deviation)) { throw std::overflow_error("sample deviation exceeds the numeric range"); }
        result.sample_standard_deviation = deviation;
        if (deviation > 0) {
            const auto sharpe = std::sqrt(252.0) * mean / deviation;
            if (!std::isfinite(sharpe)) { throw std::overflow_error("Sharpe ratio exceeds the numeric range"); }
            result.annualized_daily_sharpe = sharpe;
        }
    }
    return result;
}

namespace {

double finite_ratio(double numerator, double denominator) {
    const auto result = numerator / denominator;
    if (!std::isfinite(result)) { throw std::overflow_error("return ratio exceeds the numeric range"); }
    return result;
}

// Explicit rejection mapping avoids implementation-dependent distribution
// algorithms while preserving unbiased uniform integer block starts.
std::size_t uniform_start(std::mt19937_64& engine, std::size_t possible_starts) {
    const auto bound = static_cast<std::uint64_t>(possible_starts);
    const auto threshold = (std::uint64_t{0} - bound) % bound;
    std::uint64_t value = 0;
    do { value = engine(); } while (value < threshold);
    return static_cast<std::size_t>(value % bound);
}

} // namespace

PercentileInterval percentile_interval(std::vector<double> values) {
    if (values.empty() || std::any_of(values.begin(), values.end(), [](double x) { return !std::isfinite(x); })) {
        throw std::invalid_argument("percentile interval requires finite observations");
    }
    std::sort(values.begin(), values.end());
    auto quantile = [&](double probability) {
        const double position = probability * static_cast<double>(values.size() - 1);
        const auto lower = static_cast<std::size_t>(std::floor(position));
        const auto upper = static_cast<std::size_t>(std::ceil(position));
        const double result = values[lower] + (position - static_cast<double>(lower)) * (values[upper] - values[lower]);
        if (!std::isfinite(result)) { throw std::overflow_error("percentile interpolation exceeds the finite range"); }
        return result;
    };
    return {quantile(0.025), quantile(0.975)};
}

MovingBlockIndexSampler::MovingBlockIndexSampler(
    std::size_t sample_size, std::size_t block_length, std::uint64_t seed)
    : sample_size_{sample_size}, block_length_{block_length}, engine_{seed} {
    if (sample_size == 0 || block_length == 0 || block_length > sample_size) {
        throw std::invalid_argument("moving blocks require 1 <= block length <= sample size");
    }
}

std::vector<std::size_t> MovingBlockIndexSampler::next() {
    std::vector<std::size_t> result;
    result.reserve(sample_size_);
    while (result.size() < sample_size_) {
        const auto start = uniform_start(engine_, sample_size_ - block_length_ + 1);
        for (std::size_t offset = 0; offset < block_length_ && result.size() < sample_size_; ++offset) {
            result.push_back(start + offset);
        }
    }
    return result;
}

ReturnConcentration describe_return_concentration(std::span<const WinningContribution> days) {
    if (days.empty()) { throw std::invalid_argument("concentration requires daily returns"); }
    ReturnConcentration result{{}, 0, 0, 0, {}, {}};
    for (const auto& day : days) {
        if (!std::isfinite(day.net_return) || day.net_return <= -1) {
            throw std::invalid_argument("concentration requires finite price returns greater than -1");
        }
        result.arithmetic_net_sum += day.net_return;
        if (day.net_return > 0) {
            result.largest_five_winners.push_back(day);
            result.positive_return_sum += day.net_return;
        }
    }
    if (!std::isfinite(result.arithmetic_net_sum) || !std::isfinite(result.positive_return_sum)) {
        throw std::overflow_error("concentration sum exceeds the numeric range");
    }
    auto& winners = result.largest_five_winners;
    std::sort(winners.begin(), winners.end(), [](const auto& left, const auto& right) {
        return left.net_return != right.net_return ? left.net_return > right.net_return : left.date < right.date;
    });
    if (winners.size() > 5) { winners.resize(5); }
    for (const auto& winner : winners) { result.largest_five_net_sum += winner.net_return; }
    if (result.arithmetic_net_sum != 0) {
        result.largest_five_share_of_arithmetic_net = finite_ratio(result.largest_five_net_sum, result.arithmetic_net_sum);
    }
    if (result.positive_return_sum > 0) {
        result.largest_five_share_of_positive_returns = finite_ratio(result.largest_five_net_sum, result.positive_return_sum);
    }
    return result;
}

PeriodStatistics describe_returns(std::span<const DailyResearchReturn> days) {
    validate(days);
    std::vector<double> strategy;
    std::vector<double> control;
    std::vector<double> cash(days.size(), 0);
    std::vector<WinningContribution> contributions;
    std::size_t active = 0;
    double gross_active = 0;
    double gross_inactive = 0;
    double net_sum = 0;
    for (const auto& day : days) {
        const auto net = day.active ? day.window_net_return : 0;
        strategy.push_back(net);
        control.push_back(day.window_net_return);
        net_sum += net;
        if (day.active) { ++active; gross_active += day.window_gross_return; }
        else { gross_inactive += day.window_gross_return; }
        contributions.push_back({day.date, net});
    }
    if (!std::isfinite(gross_active) || !std::isfinite(gross_inactive)) {
        throw std::overflow_error("gross return aggregation exceeds the numeric range");
    }
    PeriodStatistics result{days.size(), active, static_cast<double>(active) / static_cast<double>(days.size()),
        describe_return_series(strategy), describe_return_series(control), describe_return_series(cash), {}, {}, {}, {}, {}, {}, net_sum, 0, {}, {}};
    if (active > 0) {
        result.mean_gross_active = gross_active / static_cast<double>(active);
        result.mean_net_active = net_sum / static_cast<double>(active);
        if (*result.mean_gross_active > 0) {
            result.break_even_cost_bps_per_side = 10000 * finite_ratio(*result.mean_gross_active, 2 + *result.mean_gross_active);
        }
    }
    if (active < days.size()) { result.mean_gross_nonpositive_signal = gross_inactive / static_cast<double>(days.size() - active); }
    if (result.mean_gross_active && result.mean_gross_nonpositive_signal) {
        result.signal_contrast_gross = *result.mean_gross_active - *result.mean_gross_nonpositive_signal;
        if (!std::isfinite(*result.signal_contrast_gross)) { throw std::overflow_error("signal contrast overflow"); }
    }
    const auto concentration = describe_return_concentration(contributions);
    result.largest_five_winners = concentration.largest_five_winners;
    result.largest_five_net_sum = concentration.largest_five_net_sum;
    result.largest_five_share_of_arithmetic_net = concentration.largest_five_share_of_arithmetic_net;
    result.largest_five_share_of_positive_returns = concentration.largest_five_share_of_positive_returns;
    return result;
}

MovingBlockBootstrap::MovingBlockBootstrap(std::size_t block_length) : block_length_{block_length} {
    if (block_length != 5 && block_length != 10) { throw std::invalid_argument("protocol bootstrap requires five- or ten-day blocks"); }
}

BootstrapStatistics MovingBlockBootstrap::run(std::span<const DailyResearchReturn> days) const {
    validate(days);
    if (days.size() < block_length_) { throw std::invalid_argument("bootstrap sample is shorter than its block length"); }
    constexpr std::size_t replicates = 10000;
    constexpr std::uint64_t seed = 20261020;
    MovingBlockIndexSampler sampler{days.size(), block_length_, seed};
    std::vector<double> means;
    std::vector<double> contrasts;
    means.reserve(replicates);
    contrasts.reserve(replicates);
    std::size_t undefined = 0;
    for (std::size_t replicate = 0; replicate < replicates; ++replicate) {
        std::size_t active = 0;
        double net = 0;
        double positive = 0;
        double nonpositive = 0;
        for (const auto index : sampler.next()) {
            const auto& day = days[index];
            if (day.active) { ++active; net += day.window_net_return; positive += day.window_gross_return; }
            else { nonpositive += day.window_gross_return; }
        }
        if (!std::isfinite(net) || !std::isfinite(positive) || !std::isfinite(nonpositive)) {
            throw std::overflow_error("bootstrap aggregation exceeds the numeric range");
        }
        means.push_back(net / static_cast<double>(days.size()));
        if (active == 0 || active == days.size()) { ++undefined; }
        else {
            const auto contrast = positive / static_cast<double>(active) - nonpositive / static_cast<double>(days.size() - active);
            if (!std::isfinite(contrast)) { throw std::overflow_error("bootstrap signal contrast overflow"); }
            contrasts.push_back(contrast);
        }
    }
    BootstrapStatistics result{block_length_, replicates, seed, percentile_interval(std::move(means)), {}, undefined};
    if (undefined == 0) { result.signal_contrast_gross_95 = percentile_interval(std::move(contrasts)); }
    return result;
}

} // namespace trading
