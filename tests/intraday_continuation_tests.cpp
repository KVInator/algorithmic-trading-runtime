#include "intraday_continuation.hpp"
#include "continuation_diagnostic_report.hpp"
#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using trading::ContinuationAction;
using trading::ContinuationVariant;
using trading::HistoricalBarRequest;
using trading::IntradayContinuationHistory;
using trading::IntradayContinuationReplayEngine;
using trading::IntradayContinuationStrategy;
using trading::MinuteBar;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void near(double actual, double expected, std::string_view message) {
    // Bounds cover a few dozen double operations, far below one basis point.
    require(std::isfinite(actual) && std::abs(actual - expected) <=
        3e-14 * std::max(1.0, std::abs(expected)), message);
}

template <typename Error, typename Operation>
void rejects(Operation operation, std::string_view message) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(std::string{message});
}

const std::vector<trading::CalendarSession>& calendar() {
    static const auto value = [] {
        auto result = trading::development_calendar({"2022-01-01", "2022-01-31"});
        const auto february = trading::development_calendar({"2022-02-01", "2022-02-28"});
        result.insert(result.end(), february.begin(), february.end());
        return result;
    }();
    return value;
}

HistoricalBarRequest session(std::size_t index) { return calendar().at(index).bar_request(); }

std::vector<MinuteBar> bars(const HistoricalBarRequest& request, double close = 100.5,
                            double volume = 100.0, double open = 100.0) {
    std::vector<MinuteBar> result;
    for (int index = 0; index < 390; ++index) {
        result.emplace_back("SPY", request.start() + std::chrono::minutes{index},
            trading::Ohlc{open, std::max(open, close), std::min(open, close), close}, volume);
    }
    return result;
}

IntradayContinuationHistory history(double close = 100.5, double volume = 100.0) {
    IntradayContinuationHistory result;
    for (std::size_t index = 0; index < 20; ++index) {
        result.append(session(index), bars(session(index), close, volume));
    }
    return result;
}

void replace(std::vector<MinuteBar>& input, std::size_t index, double open,
             double close, double volume = 100.0) {
    input[index] = MinuteBar{"SPY", input[index].start(),
        {open, std::max(open, close), std::min(open, close), close}, volume};
}

void test_history_and_same_clock_features() {
    IntradayContinuationHistory h;
    for (std::size_t index = 0; index < 20; ++index) {
        auto input = bars(session(index), 100.0 + static_cast<double>(index + 1) / 10.0,
                          100.0 * static_cast<double>(index + 1));
        for (std::size_t minute = 30; minute < input.size(); ++minute) {
            replace(input, minute, 100, 110, 1000000);
        }
        h.append(session(index), input);
        require(h.ready() == (index == 19), "only twenty prior full sessions complete warm-up");
    }
    const auto request = session(20);
    const auto current = bars(request, 102, 2100);
    const auto f = h.measure(request, std::span{current}.first(30), request.start() + std::chrono::minutes{30});
    near(f.move, 0.02, "current move uses the completed close and session open");
    near(f.typical_absolute_move, 0.0105, "twenty prior moves average to 1.05 percent");
    near(f.cumulative_volume_shares, 63000, "current volume sums thirty completed bars");
    near(f.historical_median_volume_shares, 31500, "even-count median averages the two central volumes");
    near(f.relative_volume, 2, "volume normalization must use the same clock, not full-day volume");
    require(f.oldest_history_start == session(0).start() && f.latest_history_start == session(19).start(),
            "feature evidence identifies the actual prior window");

    auto rolling = history();
    rolling.append(request, bars(request, 120));
    const auto next = session(21);
    const auto next_bars = bars(next, 102);
    const auto rolled = rolling.measure(next, std::span{next_bars}.first(30), next.start() + std::chrono::minutes{30});
    require(rolling.size() == 20 && rolled.oldest_history_start == session(1).start(),
            "the rolling history drops exactly its oldest session");
    near(rolled.typical_absolute_move, (19.0 * 0.005 + 0.2) / 20.0,
         "the rolling mean includes the new completed session once");
}

void test_information_boundary_and_failures() {
    auto h = history();
    const auto request = session(20);
    auto current = bars(request, 102, 200);
    const auto at = request.start() + std::chrono::minutes{30};
    const IntradayContinuationStrategy strategy{request, ContinuationVariant::volume_conditioned};
    const auto first = strategy.decide(h, std::span{current}.first(30), at, false);
    for (std::size_t index = 30; index < current.size(); ++index) {
        replace(current, index, 500, 1000, 1e9);
    }
    const auto changed = strategy.decide(h, std::span{current}.first(30), at, false);
    require(first.action == changed.action && first.features->move == changed.features->move &&
                first.features->relative_volume == changed.features->relative_volume,
            "all future prices and volumes are outside the decision boundary");
    rejects<std::invalid_argument>([&] { static_cast<void>(strategy.decide(h, current, at, false)); },
                                  "a full future session cannot cross the strategy boundary");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(h, std::span{current}.first(31), at, false));
    }, "a decision-minute bar is not yet complete");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(h, std::span{current}.first(29), at, false));
    }, "a missing completed interval blocks the decision");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(h, std::span{current}.first(30), at - std::chrono::seconds{1}, false));
    }, "an early timestamp cannot reveal the final signal close");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(h, std::span{current}.first(30), at + std::chrono::minutes{1}, false));
    }, "an undeclared decision clock fails");
    auto incomplete = bars(session(21));
    incomplete.erase(incomplete.begin() + 3);
    rejects<std::invalid_argument>([&] { h.append(session(21), incomplete); }, "incomplete history fails");
    require(h.size() == 20, "a failed append preserves the earlier window");
    rejects<std::invalid_argument>([&] { h.append(session(19), bars(session(19))); },
                                  "duplicate or backward history is rejected");
    auto poisoned = h;
    poisoned.append(request, bars(request));
    rejects<std::invalid_argument>([&] {
        static_cast<void>(poisoned.measure(request, std::span{current}.first(30), at));
    }, "appending the current day cannot make it available as earlier history");
    IntradayContinuationHistory cold;
    rejects<std::logic_error>([&] {
        static_cast<void>(cold.measure(request, std::span{current}.first(30), at));
    }, "warm-up is unavailable, not a manufactured cash observation");
    std::swap(current[0], current[1]);
    rejects<std::invalid_argument>([&] {
        static_cast<void>(h.measure(request, std::span{current}.first(30), at));
    }, "unordered prefix bars fail");
}

void test_decision_table_and_invariances() {
    const auto request = session(20);
    const auto at = request.start() + std::chrono::minutes{30};
    const auto h = history();
    const IntradayContinuationStrategy base{request, ContinuationVariant::price_only};
    const IntradayContinuationStrategy volume{request, ContinuationVariant::volume_conditioned};
    for (const double current_volume : {0.0, 50.0, 100.0, 200.0}) {
        const auto input = bars(request, 102, current_volume);
        const auto prefix = std::span{input}.first(30);
        require(base.decide(h, prefix, at, false).action == ContinuationAction::enter_long,
                "the base rule does not filter a valid price signal on volume");
        require(volume.decide(h, prefix, at, false).action == (current_volume > 100 ?
            ContinuationAction::enter_long : ContinuationAction::stay_cash),
            "volume equality and lower volume do not authorize fresh entry");
        require(volume.decide(h, prefix, at, true).action == ContinuationAction::hold_long,
                "declining volume must not change the common holding/exit rule");
    }
    for (const double close : {99.0, 100.0, 100.5}) {
        const auto input = bars(request, close, 200);
        require(volume.decide(h, std::span{input}.first(30), at, false).action == ContinuationAction::stay_cash,
                "negative, zero and boundary-equal moves do not enter");
        require(volume.decide(h, std::span{input}.first(30), at, true).action == ContinuationAction::exit_long,
                "the price boundary exits an existing position");
    }
    auto scaled_history = IntradayContinuationHistory{};
    for (std::size_t index = 0; index < 20; ++index) {
        scaled_history.append(session(index), bars(session(index), 201, 100000, 200));
    }
    const auto scaled = bars(request, 204, 200000, 200);
    const auto f = scaled_history.measure(request, std::span{scaled}.first(30), at);
    near(f.move, 0.02, "price scale does not change the signal return");
    near(f.typical_absolute_move, 0.005, "historical price normalization is scale invariant");
    near(f.relative_volume, 2, "common volume-unit scaling does not change relative volume");
}

std::vector<MinuteBar> two_round_trips(const HistoricalBarRequest& request) {
    auto input = bars(request, 99, 200);
    for (const std::size_t index : {29U, 59U, 119U, 149U}) { replace(input, index, 100, 101, 200); }
    replace(input, 31, 100, 99, 200);
    replace(input, 91, 102, 99, 200);
    replace(input, 121, 200, 99, 200);
    replace(input, 181, 198, 99, 200);
    replace(input, 32, 101, 99, 200);
    replace(input, 92, 103, 99, 200);
    replace(input, 122, 201, 99, 200);
    replace(input, 182, 199, 99, 200);
    return input;
}

void test_replay_cash_arithmetic_and_delay() {
    const auto h = history(100);
    const auto request = session(20);
    const IntradayContinuationStrategy strategy{request, ContinuationVariant::volume_conditioned};
    const auto input = two_round_trips(request);
    const auto result = IntradayContinuationReplayEngine{}.run(strategy, h, input);
    require(result.trades.size() == 2 && result.decisions.size() == 13,
            "the registered state path makes two round trips and a terminal cash decision");
    const auto& first = result.trades.front();
    require(first.entry_reference_time == request.start() + std::chrono::minutes{31} &&
                first.exit_reference_time == request.start() + std::chrono::minutes{91},
            "references follow decisions by the declared delay");
    require(first.entry_reference_price == 100 && first.exit_reference_price == 102,
            "future delayed opens are not substituted by signal closes");
    near(result.strategy_gross_return, 0.0098, "two and minus one percent compound to 0.98 percent");
    const double one_round_cost_factor = 9999.0 / 10001.0;
    near(result.strategy_net_return, 1.0098 * one_round_cost_factor * one_round_cost_factor - 1,
         "four charged sides and within-day reinvestment match independent cash arithmetic");
    require(result.minutes_in_market == std::chrono::minutes{120}, "holding decisions do not extend or duplicate positions");
    near(result.always_long_control.returns.gross_return, 0, "the same-window control uses 100 entry/exit references");
    require(result.always_long_control.entry_reference_time == request.start() + std::chrono::minutes{31} &&
                result.always_long_control.exit_reference_time == request.start() + std::chrono::minutes{386},
            "the control shares the full registered trading window");
    const auto delayed = IntradayContinuationReplayEngine{std::chrono::minutes{2}, 0}.run(strategy, h, input);
    near(delayed.strategy_gross_return, (103.0 / 101.0) * (199.0 / 201.0) - 1,
         "delay stress changes price references without moving decisions");
    require(delayed.trades[0].entry_decision == first.entry_decision &&
                delayed.trades[0].entry_reference_time == first.entry_reference_time + std::chrono::minutes{1},
            "signal information does not change between execution stresses");
    const auto cash = IntradayContinuationReplayEngine{}.run(strategy, h, bars(request, 99, 200));
    require(cash.trades.empty() && cash.strategy_gross_return == 0 && cash.strategy_net_return == 0 &&
                cash.minutes_in_market == std::chrono::minutes{0},
            "cash-only paths have no fictitious trade, exposure or costs");
    for (const auto delay : {std::chrono::minutes{1}, std::chrono::minutes{2}}) {
        const auto held = IntradayContinuationReplayEngine{delay, 1}.run(strategy, h, bars(request, 101, 200));
        require(held.trades.size() == 1 && held.trades[0].exit_decision == strategy.liquidation_time() &&
                    held.trades[0].exit_reference_time == strategy.liquidation_time() + delay &&
                    !held.decisions.back().features && held.decisions.back().action == ContinuationAction::exit_long,
                "a held position liquidates before close independently of a terminal feature");
        near(held.strategy_net_return, -2.0 / 10001.0, "repeated hold decisions charge only one round trip");
    }
    double previous = 1;
    for (const double cost : {0.0, 0.5, 1.0, 2.0, 5.0}) {
        const auto net = IntradayContinuationReplayEngine{std::chrono::minutes{1}, cost}.run(strategy, h, input);
        require(net.strategy_net_return < previous, "higher costs strictly reduce the same replay path");
        previous = net.strategy_net_return;
    }
}

void test_domain_errors_calendar_and_overflow() {
    const auto request = session(20);
    const auto at = request.start() + std::chrono::minutes{30};
    const auto input = bars(request, 102);
    const auto zero = history(100.5, 0);
    rejects<std::domain_error>([&] { static_cast<void>(zero.measure(request, std::span{input}.first(30), at)); },
                              "zero historical median is undefined, not a cash signal");
    auto tiny = history(100.5, std::numeric_limits<double>::min());
    const auto huge = bars(request, 102, std::numeric_limits<double>::max() / 1000);
    rejects<std::overflow_error>([&] { static_cast<void>(tiny.measure(request, std::span{huge}.first(30), at)); },
                                "overflowing volume normalization is explicit");
    auto h = history();
    const auto overflow = bars(request, 100, std::numeric_limits<double>::max());
    rejects<std::overflow_error>([&] { h.append(request, overflow); }, "overflowing cumulative history volume fails");
    const auto price_overflow = bars(request, std::numeric_limits<double>::max(), 1,
                                     std::numeric_limits<double>::min());
    rejects<std::overflow_error>([&] { h.append(request, price_overflow); }, "overflowing history price moves fail");
    require(h.size() == 20, "numeric failures preserve historical state");
    for (const int delay : {-1, 0, 3, 30}) {
        rejects<std::invalid_argument>([&] {
            static_cast<void>(IntradayContinuationReplayEngine{std::chrono::minutes{delay}, 1});
        }, "undeclared or overlapping execution delays fail");
    }
    for (const double cost : {-1.0, 10000.0, std::numeric_limits<double>::infinity()}) {
        rejects<std::invalid_argument>([&] { static_cast<void>(IntradayContinuationReplayEngine{std::chrono::minutes{1}, cost}); },
                                      "invalid cost settings use the common rejection contract");
    }
    rejects<std::invalid_argument>([&] {
        static_cast<void>(IntradayContinuationStrategy{request, static_cast<ContinuationVariant>(9)});
    }, "unknown variants cannot silently select a trading rule");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(IntradayContinuationStrategy{HistoricalBarRequest{"QQQ", request.start(), request.end_exclusive()},
                                                       ContinuationVariant::price_only});
    }, "the registered instrument is explicit");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(IntradayContinuationStrategy{HistoricalBarRequest{"SPY", request.start(), request.start() +
            std::chrono::minutes{210}}, ContinuationVariant::price_only});
    }, "a shortened session is not stretched to a full grid");
    const trading::CalendarSession summer{"2022-03-14", "09:30", "16:00"};
    const auto summer_bars = bars(summer.bar_request(), 101, 200);
    const IntradayContinuationStrategy summer_strategy{summer.bar_request(), ContinuationVariant::price_only};
    const auto summer_result = IntradayContinuationReplayEngine{}.run(summer_strategy, h, summer_bars);
    require(summer_result.trades.front().entry_reference_time == trading::parse_utc_seconds("2022-03-14T14:01:00Z"),
            "summer clocks derive from the supplied New York calendar session");
    rejects<std::invalid_argument>([&] {
        static_cast<void>(IntradayContinuationStrategy{HistoricalBarRequest{"SPY", summer.open_utc() + std::chrono::hours{1},
            summer.close_utc() + std::chrono::hours{1}}, ContinuationVariant::price_only});
    }, "a winter UTC offset on a summer date is rejected");
    auto incomplete = input;
    incomplete.pop_back();
    rejects<std::invalid_argument>([&] {
        static_cast<void>(IntradayContinuationReplayEngine{}.run(
            IntradayContinuationStrategy{request, ContinuationVariant::price_only}, h, incomplete));
    }, "missing final coverage blocks the whole replay");
    require(trading::continuation_protocol_sha256().size() == 64, "the compiled study identity is retained");
}

void test_report_contract() {
    const auto request = session(20);
    const auto h = history(100);
    const auto input = two_round_trips(request);
    const auto price = IntradayContinuationReplayEngine{}.run(
        {request, ContinuationVariant::price_only}, h, input);
    auto volume = IntradayContinuationReplayEngine{}.run(
        {request, ContinuationVariant::volume_conditioned}, h, input);
    const auto html = trading::format_continuation_diagnostic_html(request, price, volume);
    require(html.find("data-kind=\"synthetic\"") != std::string::npos &&
                html.find("not market results") != std::string::npos && html.find("<script") == std::string::npos,
            "a diagnostic figure cannot be presented as observed market performance");
    require(html.find("price-chart") != std::string::npos && html.find("volume-chart") != std::string::npos,
            "the visual report contains both feature comparisons");
    const auto csv = trading::format_continuation_diagnostic_csv(price, volume);
    std::size_t lines = 0;
    std::size_t begin = 0;
    while (begin < csv.size()) {
        const auto end = csv.find('\n', begin);
        require(end != std::string::npos && std::count(csv.begin() + static_cast<std::ptrdiff_t>(begin),
            csv.begin() + static_cast<std::ptrdiff_t>(end), ',') == 8,
            "every diagnostic CSV row preserves the nine-column schema including terminal nulls");
        ++lines;
        begin = end + 1;
    }
    require(lines == 27, "the CSV retains the header and thirteen decisions for each variant");
    volume.decisions.front().features->relative_volume += 1;
    rejects<std::invalid_argument>([&] {
        static_cast<void>(trading::format_continuation_diagnostic_html(request, price, volume));
    }, "a paired report cannot silently compare different input features");
}

} // namespace

int main() {
    try {
        test_history_and_same_clock_features();
        test_information_boundary_and_failures();
        test_decision_table_and_invariances();
        test_replay_cash_arithmetic_and_delay();
        test_domain_errors_calendar_and_overflow();
        test_report_contract();
        std::cout << "Intraday continuation contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Intraday continuation contract failure: " << error.what() << '\n';
        return 1;
    }
}
