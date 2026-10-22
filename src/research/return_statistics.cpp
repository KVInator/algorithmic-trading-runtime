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

ReturnStatistics describe_series(std::span<const double> values) {
    double sum = 0;
    double wealth = 1;
    double peak = 1;
    double drawdown = 0;
    for (const double value : values) {
        sum += value;
        wealth *= 1 + value;
        if (!std::isfinite(sum) || !std::isfinite(wealth) || wealth <= 0) {
            throw std::range_error("return aggregation exceeds the positive finite wealth range");
        }
        peak = std::max(peak, wealth);
        drawdown = std::max(drawdown, 1 - wealth / peak);
    }
    const bool constant = std::all_of(values.begin(), values.end(), [&](double value) { return value == values.front(); });
    const auto mean = constant ? values.front() : sum / static_cast<double>(values.size());
    ReturnStatistics result{mean, wealth - 1, drawdown, {}, {}};
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

PercentileInterval interval(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    auto quantile = [&](double probability) {
        const double position = probability * static_cast<double>(values.size() - 1);
        const auto lower = static_cast<std::size_t>(std::floor(position));
        const auto upper = static_cast<std::size_t>(std::ceil(position));
        return values[lower] + (position - static_cast<double>(lower)) * (values[upper] - values[lower]);
    };
    return {quantile(0.025), quantile(0.975)};
}

} // namespace

PeriodStatistics describe_returns(std::span<const DailyResearchReturn> days) {
    validate(days);
    std::vector<double> strategy;
    std::vector<double> control;
    std::vector<double> cash(days.size(), 0);
    std::vector<WinningContribution> winners;
    std::size_t active = 0;
    double gross_active = 0;
    double gross_inactive = 0;
    double net_sum = 0;
    double positive_net_sum = 0;
    for (const auto& day : days) {
        const auto net = day.active ? day.window_net_return : 0;
        strategy.push_back(net);
        control.push_back(day.window_net_return);
        net_sum += net;
        if (day.active) { ++active; gross_active += day.window_gross_return; }
        else { gross_inactive += day.window_gross_return; }
        if (net > 0) { winners.push_back({day.date, net}); positive_net_sum += net; }
    }
    if (!std::isfinite(gross_active) || !std::isfinite(gross_inactive) || !std::isfinite(positive_net_sum)) {
        throw std::overflow_error("gross return aggregation exceeds the numeric range");
    }
    PeriodStatistics result{days.size(), active, static_cast<double>(active) / static_cast<double>(days.size()),
        describe_series(strategy), describe_series(control), describe_series(cash), {}, {}, {}, {}, {}, {}, net_sum, 0, {}, {}};
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
    std::sort(winners.begin(), winners.end(), [](const auto& left, const auto& right) {
        return left.net_return != right.net_return ? left.net_return > right.net_return : left.date < right.date;
    });
    if (winners.size() > 5) { winners.resize(5); }
    result.largest_five_winners = std::move(winners);
    for (const auto& winner : result.largest_five_winners) { result.largest_five_net_sum += winner.net_return; }
    if (net_sum != 0) { result.largest_five_share_of_arithmetic_net = finite_ratio(result.largest_five_net_sum, net_sum); }
    if (positive_net_sum > 0) { result.largest_five_share_of_positive_returns = finite_ratio(result.largest_five_net_sum, positive_net_sum); }
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
    std::mt19937_64 engine{seed};
    std::vector<double> means;
    std::vector<double> contrasts;
    means.reserve(replicates);
    contrasts.reserve(replicates);
    std::size_t undefined = 0;
    for (std::size_t replicate = 0; replicate < replicates; ++replicate) {
        std::size_t sampled = 0;
        std::size_t active = 0;
        double net = 0;
        double positive = 0;
        double nonpositive = 0;
        while (sampled < days.size()) {
            const auto start = uniform_start(engine, days.size() - block_length_ + 1);
            for (std::size_t offset = 0; offset < block_length_ && sampled < days.size(); ++offset, ++sampled) {
                const auto& day = days[start + offset];
                if (day.active) { ++active; net += day.window_net_return; positive += day.window_gross_return; }
                else { nonpositive += day.window_gross_return; }
            }
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
    BootstrapStatistics result{block_length_, replicates, seed, interval(std::move(means)), {}, undefined};
    if (undefined == 0) { result.signal_contrast_gross_95 = interval(std::move(contrasts)); }
    return result;
}

} // namespace trading
