#include "intraday_momentum.hpp"
#include "minute_bar_csv.hpp"
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

using trading::parse_utc_seconds;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

void near(double actual, double expected, std::string_view message) {
    // A few double rounding steps at the price/return scale are allowed;
    // the bound remains far below a cent or one basis point in these fixtures.
    require(std::abs(actual - expected) <= 2e-15 * std::max(1.0, std::abs(expected)), message);
}

template <typename Error, typename Operation>
void expect_failure(Operation operation, std::string_view message) {
    try {
        operation();
    } catch (const Error&) {
        return;
    }
    throw std::runtime_error(std::string{message});
}

trading::HistoricalBarRequest session(bool summer = false) {
    return {"SPY", parse_utc_seconds(summer ? "2022-03-14T13:30:00Z" : "2023-01-03T14:30:00Z"),
            parse_utc_seconds(summer ? "2022-03-14T20:00:00Z" : "2023-01-03T21:00:00Z")};
}

// Synthetic data with independently chosen prices at the signal and fill clocks.
std::vector<trading::MinuteBar> fixture(double signal_close = 101, bool summer = false) {
    std::vector<trading::MinuteBar> bars;
    const auto start = session(summer).start();
    for (int minute = 0; minute < 390; ++minute) {
        double open = 100;
        if (minute == 360) { open = 1000; } // Decision-minute open must not be used.
        if (minute == 362) { open = 101; }  // Two-minute delayed entry.
        if (minute == 386) { open = 102; }  // One-minute delayed exit.
        if (minute == 387) { open = 100; }  // Two-minute delayed exit.
        const double close = minute == 359 ? signal_close : 100;
        bars.emplace_back("SPY", start + std::chrono::minutes{minute},
                          trading::Ohlc{open, std::max(open, close) + 1,
                                        std::min(open, close) - 1, close}, 1000.25);
    }
    return bars;
}

void test_analytical_prices() {
    const auto gross = trading::evaluate_long_prices(100, 102, 0);
    near(gross.gross_return, 0.02, "100-to-102 gross return must be two percent");
    require(gross.gross_return == gross.net_return, "zero cost must preserve gross return");
    const auto net = trading::evaluate_long_prices(100, 102, 1);
    near(net.buy_after_cost, 100.01, "one-basis-point entry cost must increase the buy price");
    near(net.sell_after_cost, 101.9898, "one-basis-point exit cost must reduce the sell price");
    // Exact decimal cash difference is 1.9798 on a 100.01 purchase.
    near(net.net_return, 19798.0 / 1000100.0, "both sides of the trade must be charged");
    const auto flat = trading::evaluate_long_prices(100, 100, 1);
    near(flat.gross_return, 0, "flat prices have zero gross return");
    near(flat.net_return, -2.0 / 10001.0, "a flat-price round trip must lose both one-way costs");
    near(trading::evaluate_long_prices(100, 95, 0).net_return, -0.05,
         "falling prices must preserve a loss rather than clip it");
    double previous = std::numeric_limits<double>::infinity();
    for (const double cost : {0.0, 0.5, 1.0, 2.0, 5.0}) {
        const double result = trading::evaluate_long_prices(100, 102, cost).net_return;
        require(result < previous, "net return must decrease as cost increases");
        previous = result;
        near(trading::evaluate_long_prices(200, 204, cost).net_return, result,
             "continuous-notional returns must be invariant to the price scale");
    }
}

void test_signal_and_delay() {
    const trading::IntradayMomentumStrategy strategy{session()};
    const auto bars = fixture();
    const auto result = trading::DelayedBarReplay{}.run(strategy, bars);
    require(result.decision.enter_long && result.strategy_trade.has_value(),
            "a positive completed-session signal must produce one long round trip");
    near(result.decision.signal_return, 0.01, "the signal must use 100 opening and 101 completed prices");
    const auto& trade = *result.strategy_trade;
    require(trade.entry_decision == parse_utc_seconds("2023-01-03T20:30:00Z") &&
                trade.entry_reference_time == parse_utc_seconds("2023-01-03T20:31:00Z") &&
                trade.exit_decision == parse_utc_seconds("2023-01-03T20:55:00Z") &&
                trade.exit_reference_time == parse_utc_seconds("2023-01-03T20:56:00Z"),
            "winter decision and reference clocks must match the protocol");
    require(trade.entry_reference_price == 100 && trade.exit_reference_price == 102,
            "replay must use the delayed opens rather than decision-minute prices");
    near(result.strategy_net_return(), 19798.0 / 1000100.0, "active-day research return must match cash arithmetic");
    require(result.strategy_net_return() == result.always_long_control.returns.net_return,
            "strategy and control must share the same return calculation on an active day");
    const auto delayed = trading::DelayedBarReplay{std::chrono::minutes{2}, 0}.run(strategy, bars);
    require(delayed.strategy_trade->entry_reference_time == parse_utc_seconds("2023-01-03T20:32:00Z") &&
                delayed.strategy_trade->exit_reference_time == parse_utc_seconds("2023-01-03T20:57:00Z"),
            "delay stress must move both price references without changing decision times");
    near(delayed.strategy_net_return(), -1.0 / 101.0,
         "two-minute references of 101 and 100 must produce the correct loss");

    const auto summer = trading::DelayedBarReplay{}.run(trading::IntradayMomentumStrategy{session(true)},
                                                        fixture(101, true));
    require(summer.decision.at == parse_utc_seconds("2022-03-14T19:30:00Z") &&
                summer.strategy_trade->entry_reference_time == parse_utc_seconds("2022-03-14T19:31:00Z") &&
                summer.strategy_trade->exit_reference_time == parse_utc_seconds("2022-03-14T19:56:00Z"),
            "an explicitly supplied summer window must preserve New York trading clocks");
}

void test_cash_and_information_boundary() {
    const trading::IntradayMomentumStrategy strategy{session()};
    for (const double signal_close : {99.0, 100.0}) {
        const auto result = trading::DelayedBarReplay{}.run(strategy, fixture(signal_close));
        require(!result.decision.enter_long && !result.strategy_trade &&
                    result.strategy_gross_return() == 0 && result.strategy_net_return() == 0,
                "negative and zero signals must stay in cash without fictitious fills or costs");
        require(result.always_long_control.returns.net_return > 0,
                "the always-long control must still be calculated on cash days");
    }
    auto bars = fixture();
    const auto first = trading::DelayedBarReplay{}.run(strategy, bars);
    for (std::size_t index = 360; index < bars.size(); ++index) {
        const auto& original = bars[index];
        bars[index] = trading::MinuteBar{"SPY", original.start(), {100, 300, 1, 250}, 1000000};
    }
    const auto changed = trading::DelayedBarReplay{}.run(strategy, bars);
    require(changed.decision.signal_return == first.decision.signal_return && changed.decision.enter_long,
            "future OHLCV changes must not alter the signal");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(bars, strategy.entry_decision_time()));
    }, "passing future bars into the signal boundary must fail");
    const auto history = std::span<const trading::MinuteBar>{bars}.first(360);
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(history, strategy.entry_decision_time() - std::chrono::seconds{1}));
    }, "the 15:29 close must not be usable before the bar completes");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(strategy.decide(history.first(359), strategy.entry_decision_time()));
    }, "an incomplete signal history must fail");
}

void test_invalid_inputs() {
    const trading::IntradayMomentumStrategy strategy{session()};
    for (const int index : {0, 359, 361, 386, 389}) {
        auto bars = fixture();
        bars.erase(bars.begin() + index);
        expect_failure<std::invalid_argument>([&] { static_cast<void>(trading::DelayedBarReplay{}.run(strategy, bars)); },
                                              "a missing session minute must block replay");
    }
    auto bars = fixture();
    std::swap(bars[360], bars[361]);
    expect_failure<std::invalid_argument>([&] { static_cast<void>(trading::DelayedBarReplay{}.run(strategy, bars)); },
                                          "out-of-order records must fail");
    bars = fixture();
    bars[361] = bars[360];
    expect_failure<std::invalid_argument>([&] { static_cast<void>(trading::DelayedBarReplay{}.run(strategy, bars)); },
                                          "duplicate records must fail");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::IntradayMomentumStrategy{trading::HistoricalBarRequest{
            "QQQ", session().start(), session().end_exclusive()}});
    }, "the fixed SPY protocol must reject another instrument");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::IntradayMomentumStrategy{trading::HistoricalBarRequest{
            "SPY", session().start(), session().start() + std::chrono::minutes{210}}});
    }, "shortened sessions must not be treated as full sessions");
    for (const int delay : {-1, 0, 3}) {
        expect_failure<std::invalid_argument>([&] {
            static_cast<void>(trading::DelayedBarReplay{std::chrono::minutes{delay}, 1});
        }, "undeclared replay delays must fail");
    }
    for (const double cost : {-1.0, 10000.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
        expect_failure<std::invalid_argument>([&] { static_cast<void>(trading::evaluate_long_prices(100, 101, cost)); },
                                              "invalid costs must fail");
    }
    expect_failure<std::invalid_argument>([&] { static_cast<void>(trading::evaluate_long_prices(0, 100, 1)); },
                                          "zero price references must fail");
    expect_failure<std::range_error>([&] {
        static_cast<void>(trading::evaluate_long_prices(std::numeric_limits<double>::max(), 100, 1));
    }, "overflowing cost-adjusted prices must fail");
    expect_failure<std::overflow_error>([&] {
        static_cast<void>(trading::evaluate_long_prices(std::numeric_limits<double>::min(),
                                                       std::numeric_limits<double>::max(), 0));
    }, "overflowing price ratios must fail");
}

void test_csv_contracts() {
    const auto original = fixture();
    const auto csv = trading::format_minute_bars_csv(original);
    const auto parsed = trading::parse_minute_bars_csv(csv);
    require(parsed.size() == original.size(), "CSV serialization must retain every bar");
    for (std::size_t index = 0; index < original.size(); ++index) {
        const auto& left = original[index];
        const auto& right = parsed[index];
        require(left.symbol() == right.symbol() && left.start() == right.start() &&
                    left.prices().open == right.prices().open && left.prices().high == right.prices().high &&
                    left.prices().low == right.prices().low && left.prices().close == right.prices().close &&
                    left.volume() == right.volume(), "all CSV fields must round-trip exactly");
    }
    std::string crlf;
    for (const char character : csv) { if (character == '\n') { crlf += '\r'; } crlf += character; }
    require(trading::parse_minute_bars_csv(crlf).size() == 390, "Windows CRLF must be accepted");
    require(trading::parse_minute_bars_csv(csv.substr(0, csv.size() - 1)).size() == 390,
            "a final newline must not be required");
    constexpr std::string_view header = "symbol,timestamp_utc,open,high,low,close,volume_shares\n";
    for (const std::string_view row : {
             "SPY,2023-01-03T14:30:00Z,100,102,99,101\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,101,1,2\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,101,\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,101,nan\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,101,-1\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,101,1x\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,103,1\n",
             "SPY,2023-01-03T14:30:00Z,100,102,99,101, 1\n",
             "SPY,2023-01-03T14:30:01Z,100,102,99,101,1\n",
             "\"SPY\",2023-01-03T14:30:00Z,100,102,99,101,1\n", "\n"}) {
        expect_failure<std::invalid_argument>([&] {
            static_cast<void>(trading::parse_minute_bars_csv(std::string{header} + std::string{row}));
        }, "malformed CSV values, schema and bar invariants must be rejected");
    }
    expect_failure<std::invalid_argument>([&] { static_cast<void>(trading::parse_minute_bars_csv("wrong header")); },
                                          "unexpected headers must fail");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::parse_minute_bars_csv(std::string(1024U * 1024U + 1, 'x')));
    }, "oversized CSV input must fail");
}

} // namespace

int main() {
    try {
        test_analytical_prices();
        test_signal_and_delay();
        test_cash_and_information_boundary();
        test_invalid_inputs();
        test_csv_contracts();
        std::cout << "Momentum replay contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Momentum replay contract failure: " << error.what() << '\n';
        return 1;
    }
}
