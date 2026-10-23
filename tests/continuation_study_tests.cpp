#include "continuation_study_report.hpp"
#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

using namespace std::chrono;
using trading::ContinuationVariant;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void near(double actual, double expected, std::string_view message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= 2e-12 * std::max(1.0, std::abs(expected)), message);
}

template<class Error, class Function>
void rejects(Function operation, std::string_view message) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(std::string{message});
}

std::vector<trading::SessionDownload> sessions() {
    auto calendar = trading::development_calendar({"2022-01-01", "2022-01-31"});
    for (const auto& range : {trading::CalendarRange{"2022-02-01", "2022-02-28"}, trading::CalendarRange{"2022-03-01", "2022-03-31"}}) {
        const auto month_sessions = trading::development_calendar(range);
        calendar.insert(calendar.end(), month_sessions.begin(), month_sessions.end());
    }
    std::vector<trading::SessionDownload> result;
    for (std::size_t day = 0; day < 50; ++day) {
        const auto request = calendar.at(day).bar_request();
        std::vector<trading::MinuteBar> bars;
        for (int minute = 0; minute < 390; ++minute) {
            double open = 100;
            double close = day < 20 ? 100.5 : 100.1;
            if (day >= 20) {
                if (minute % 30 == 29) { close = (minute / 30) % 3 == 2 ? 99.5 : 102; }
                if (minute % 30 == 1 || minute % 30 == 2 || minute >= 386) {
                    open = 100 + 0.02 * static_cast<double>(minute) + 0.01 * static_cast<double>(day);
                }
            }
            bars.emplace_back("SPY", request.start() + minutes{minute},
                trading::Ohlc{open, std::max(open, close), std::min(open, close), close},
                day < 20 ? 100 : day % 2 == 0 ? 200 : 50);
        }
        const auto audit = trading::audit_session(request, bars);
        result.push_back({request, std::move(bars), {}, audit, request.start(), request.end_exclusive()});
    }
    return result;
}

void test_paired_replay() {
    auto input = sessions();
    const auto days = trading::ContinuationStudy{}.replay(input);
    require(days.size() == 30, "twenty warm-up sessions must not become cash observations");
    for (std::size_t d = 0; d < days.size(); ++d) {
        const auto& day = days[d];
        require(day.date == trading::format_utc_seconds(input[d + 20].request.start()).substr(0, 10), "scored date mapping");
        for (std::size_t clock = 0; clock < 12; ++clock) {
            const auto& opportunity = day.opportunities[clock];
            require(opportunity.features.oldest_history_start == input[d].request.start(), "rolling oldest history identity");
            require(opportunity.features.latest_history_start == input[d + 19].request.start(), "current day must not enter its own history");
            for (std::size_t delay = 0; delay < 2; ++delay) {
                const auto& forward = opportunity.forward[delay];
                const auto exit_minute = clock == 11 ? 385 : 30 * static_cast<int>(clock + 2);
                require(forward.exit_reference_time == input[d + 20].request.start() + minutes{exit_minute + static_cast<int>(delay) + 1},
                    "forward window ends at next delayed decision or terminal reference");
                near(forward.returns.gross_return, forward.exit_reference_price / forward.entry_reference_price - 1,
                    "forward diagnostic uses gross open-to-open return");
            }
        }
        for (std::size_t i = 0; i < 20; ++i) {
            const auto& spec = trading::continuation_cases()[i];
            const auto& value = day.cases[i];
            const double c = spec.cost_bps_per_side / 10000;
            double gross = 1, net = 1, turnover = 0;
            minutes held{0};
            for (const auto& trade : value.trades) {
                const double ratio = trade.exit_reference_price / trade.entry_reference_price;
                turnover += net * (1 + ratio) / (1 + c);
                gross *= ratio;
                net *= ratio * (1 - c) / (1 + c);
                held += duration_cast<minutes>(trade.exit_reference_time - trade.entry_reference_time);
                require(trade.entry_reference_time - trade.entry_decision == spec.delay, "entry delay");
                require(trade.exit_reference_time - trade.exit_decision == spec.delay, "exit delay");
            }
            near(value.gross_return, gross - 1, "batch gross compounds all round trips");
            near(value.net_return, net - 1, "independent multiplicative cost oracle");
            near(value.turnover, turnover, "notional turnover follows reinvested equity");
            require(value.minutes_in_market == held, "held minutes use reference times");
            if (spec.cost_bps_per_side > 0) {
                const auto zero = trading::continuation_case_index(spec.variant, spec.delay, 0);
                require(value.trades.size() == day.cases[zero].trades.size(), "costs cannot change decisions");
                require(value.net_return <= day.cases[zero].net_return, "positive costs cannot improve a fixed trade path");
            }
        }
    }
    const auto first = days.front().cases[2].net_return;
    input.back().bars.back() = trading::MinuteBar{"SPY", input.back().bars.back().start(), {100, 1000, 99, 999}, 100000};
    near(trading::ContinuationStudy{}.replay(input).front().cases[2].net_return, first, "later sessions cannot change earlier replay");
    input = sessions();
    input[20].bars.erase(input[20].bars.begin() + 3);
    rejects<std::invalid_argument>([&] { static_cast<void>(trading::ContinuationStudy{}.replay(input)); }, "incomplete scored session must fail, not become cash");
    input = sessions();
    std::swap(input[0], input[1]);
    rejects<std::invalid_argument>([&] { static_cast<void>(trading::ContinuationStudy{}.replay(input)); }, "unordered warm-up must fail");
    trading::FrozenDevelopmentDataset invalid{};
    rejects<std::invalid_argument>([&] { static_cast<void>(trading::ContinuationStudy{}.run_frozen(invalid)); }, "wrong snapshot rejected before scoring");
}

// Statistical records deliberately isolate group contrasts and paired sampling
// from the state-machine contract exercised above.
std::vector<trading::ContinuationDay> statistical_days() {
    std::vector<trading::ContinuationDay> days;
    for (std::size_t i = 0; i < 30; ++i) {
        trading::ContinuationDay day{};
        day.date = trading::format_utc_seconds(sys_days{year{2022}/January/1} + std::chrono::days{static_cast<int>(i)}).substr(0, 10);
        for (auto& value : day.cases) {
            value.gross_return = 0.001;
            value.net_return = 0.001;
            value.control_gross_return = 0.0005;
            value.control_net_return = 0.0005;
            value.trades.resize(1);
            value.minutes_in_market = minutes{60};
            value.turnover = 2;
        }
        for (std::size_t clock = 0; clock < 12; ++clock) {
            const auto group = (i + clock) % 3;
            auto& opportunity = day.opportunities[clock];
            opportunity.features.move = group == 0 ? 0 : 2;
            opportunity.features.typical_absolute_move = 1;
            opportunity.features.relative_volume = group == 1 ? 2 : 0.5;
            const double r = (group == 0 ? -0.002 : group == 1 ? 0.003 : 0.001) + 0.00001 * static_cast<double>(clock);
            opportunity.forward[0].returns.gross_return = r;
            opportunity.forward[1].returns.gross_return = r;
        }
        days.push_back(std::move(day));
    }
    return days;
}

void test_statistics_and_report() {
    const auto study = trading::ContinuationStudy{}.analyze(statistical_days());
    near(*study.contrasts.price, 0.004, "equal-weight within-clock price contrast");
    near(*study.contrasts.volume, 0.002, "volume contrast conditions on true price");
    near(study.statistics[2].overall.net.mean, 0.001, "cash-inclusive daily denominator");
    require(!study.statistics[2].overall.net.annualized_daily_sharpe, "constant series has undefined Sharpe");
    near(study.statistics[2].overall.net.compounded, std::pow(1.001, 30) - 1, "across-day compounding");
    require(study.variants[0].status == "insufficient_active_days" && !study.variants[0].later_evaluation_allowed,
        "short samples cannot pass continuation gates");
    for (const auto& b : study.bootstrap) {
        require(b.replicates == 10000 && b.seed == 20261021, "new protocol bootstrap identity");
        near(b.paired_net_increment_95.lower, 0, "identical paired returns give exactly zero uncertainty");
        near(b.paired_net_increment_95.upper, 0, "paired indices cannot be independently resampled");
        near(b.price_net_mean_95.lower, 0.001, "constant mean bootstrap analytical limit");
        near(b.control_net_mean_95.upper, 0.0005, "control uses the same sampled sessions");
        require(b.undefined_price_replicates == 0 && b.undefined_volume_replicates == 0, "periodic complete groups remain defined in blocks");
        near(b.volume_contrast_95->lower, 0.002, "within-group constant contrast bootstrap limit");
    }
    require(!study.volume_increment.point_gates.front().passed, "equal net returns do not establish volume improvement");
    auto screen = study;
    auto& primary = screen.statistics[2];
    primary.overall.sessions = 200;
    primary.overall.active_days = 100;
    primary.overall.concentration.largest_five_share_of_positive_returns = 0.25;
    primary.overall.concentration.arithmetic_net_sum = 0.2;
    primary.overall.concentration.largest_five_net_sum = 0.05;
    primary.years["2023"] = primary.overall;
    for (auto& b : screen.bootstrap) { b.price_contrast_95 = trading::PercentileInterval{0.001, 0.005}; }
    require(trading::assess_continuation_variant(screen, ContinuationVariant::price_only).later_evaluation_allowed,
        "all declared point and uncertainty gates can pass");
    primary.overall.concentration.largest_five_share_of_positive_returns = 0.50001;
    require(!trading::assess_continuation_variant(screen, ContinuationVariant::price_only).later_evaluation_allowed,
        "concentration above one half blocks escalation");
    primary.overall.concentration.largest_five_share_of_positive_returns = 0.5;
    require(trading::assess_continuation_variant(screen, ContinuationVariant::price_only).later_evaluation_allowed,
        "concentration equality at one half passes");
    primary.years.at("2023").net.mean = -0.0001;
    require(!trading::assess_continuation_variant(screen, ContinuationVariant::price_only).later_evaluation_allowed,
        "one negative calendar year blocks escalation");
    primary.years.at("2023").net.mean = 0.001;
    screen.bootstrap[1].price_net_mean_95.lower = 0;
    require(!trading::assess_continuation_variant(screen, ContinuationVariant::price_only).later_evaluation_allowed,
        "zero lower uncertainty bound does not pass");
    primary.overall.gross.mean = 0;
    require(trading::assess_continuation_variant(screen, ContinuationVariant::price_only).status == "no_gross_edge",
        "non-positive gross evidence stops the candidate");
    primary.overall.gross.mean = 0.001;
    primary.overall.net.mean = 0;
    require(trading::assess_continuation_variant(screen, ContinuationVariant::price_only).status == "cost_dominated",
        "positive gross with non-positive net is cost-dominated");
    const auto html = trading::ContinuationStudyReport{study}.html({"synthetic_contract", "fixture", "feature-fixture"});
    require(html.find("constructed observations, not market results") != std::string::npos, "synthetic report must be explicit");
    require(html.find("<script") == std::string::npos && html.find("https://") == std::string::npos, "self-contained report has no scripts or external assets");
    require(html.find("Daily-close drawdown") != std::string::npos || html.find("daily-close drawdown") != std::string::npos, "drawdown visualization present");
    const auto csv = trading::ContinuationStudyReport{study}.daily_csv();
    require(std::count(csv.begin(), csv.end(), '\n') == 601, "all thirty days and twenty cases are exported");
    const auto features = trading::ContinuationStudyReport{study}.features_csv();
    require(std::count(features.begin(), features.end(), '\n') == 721, "all clocks and both forward delays are exported");
    auto incomplete = statistical_days();
    for (auto& day : incomplete) { day.opportunities[0].features.move = 2; }
    const auto undefined = trading::ContinuationStudy{}.analyze(std::move(incomplete));
    require(!undefined.contrasts.price, "one missing clock group invalidates the whole price contrast");
    for (const auto& b : undefined.bootstrap) {
        require(!b.price_contrast_95 && b.undefined_price_replicates == 10000, "undefined resamples must not be dropped");
    }
    auto invalid = statistical_days();
    invalid[0].cases[0].gross_return = std::numeric_limits<double>::quiet_NaN();
    rejects<std::invalid_argument>([&] { static_cast<void>(trading::ContinuationStudy{}.analyze(invalid)); }, "non-finite daily returns fail");
}

void test_shared_statistics() {
    const std::array<double, 3> returns{0.1, -0.2, 0.05};
    const auto stats = trading::describe_return_series(returns);
    near(stats.compounded, -0.076, "analytical wealth path");
    near(stats.maximum_drawdown, 0.2, "drawdown includes previous peak");
    const std::array<double, 1> loss{-0.1};
    near(trading::describe_return_series(loss).maximum_drawdown, 0.1, "initial capital participates in drawdown");
    const auto interval = trading::percentile_interval({0, 10});
    near(interval.lower, 0.25, "interpolated lower percentile");
    near(interval.upper, 9.75, "interpolated upper percentile");
    trading::MovingBlockIndexSampler single{10, 10, 42};
    const auto indices = single.next();
    for (std::size_t i = 0; i < indices.size(); ++i) { require(indices[i] == i, "one possible block has no wraparound"); }
    trading::MovingBlockIndexSampler sampler{13, 5, 42};
    const auto blocks = sampler.next();
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        require(blocks[i] < 13, "non-circular block stays inside sample");
        if (i % 5 != 0) { require(blocks[i] == blocks[i - 1] + 1, "indices remain contiguous inside blocks"); }
    }
    rejects<std::invalid_argument>([] { static_cast<void>(trading::MovingBlockIndexSampler{4, 5, 0}); }, "block longer than sample fails");
    rejects<std::invalid_argument>([] { static_cast<void>(trading::percentile_interval({})); }, "empty percentiles fail");
    rejects<std::overflow_error>([] { static_cast<void>(trading::percentile_interval(
        {-std::numeric_limits<double>::max(), std::numeric_limits<double>::max()})); }, "percentile overflow must fail explicitly");
    const std::array<trading::WinningContribution, 7> contributions{{{"a", 0.01}, {"b", 0.02}, {"c", 0.03},
        {"d", 0.04}, {"e", 0.05}, {"f", 0.06}, {"g", 0.07}}};
    const auto concentration = trading::describe_return_concentration(contributions);
    near(concentration.arithmetic_net_sum, 0.28, "arithmetic contribution denominator");
    near(concentration.largest_five_net_sum, 0.25, "five largest positive returns");
    near(*concentration.largest_five_share_of_positive_returns, 0.25 / 0.28, "positive-return concentration denominator");
}

} // namespace

int main() {
    try {
        test_shared_statistics();
        test_paired_replay();
        test_statistics_and_report();
        std::cout << "Continuation paired-study contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Continuation study contracts failed: " << error.what() << '\n';
        return 1;
    }
}
