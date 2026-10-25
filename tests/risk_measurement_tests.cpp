#include "daily_risk_study.hpp"
#include "utc_timestamp.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
void near(double a, double b, double tolerance = 1e-12) { require(std::abs(a-b) <= tolerance, "analytical risk identity failed"); }
template<class F> void rejects(F f) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; }
    require(failed, "invalid risk reference must stop");
}
trading::RiskSessionReferences constant(const std::string& date, double price, int missing = -1, bool shortened = false) {
    const trading::CalendarSession calendar{date, "09:30", shortened ? "13:00" : "16:00"};
    std::vector<trading::MinuteBar> bars;
    int i = 0;
    for (auto at = calendar.open_utc(); at < calendar.close_utc(); at += std::chrono::minutes{1}, ++i) {
        if (i != missing) { bars.emplace_back("SPY", at, trading::Ohlc{price, price, price, price}, 1); }
    }
    return {calendar, std::move(bars)};
}
void measurements() {
    const auto session = constant("2023-06-02", 101);
    const auto m = trading::measure_daily_risk(session, 100, 0);
    const auto r = std::log(1.01);
    near(m.log_return, r); near(m.daily2, r*r);
    require(m.rv1 && m.rv5 && m.rv15, "complete endpoint partitions must remain available");
    near(*m.rv1, r*r); near(*m.rv5, r*r); near(*m.rv15, r*r);
    const auto ex = trading::measure_daily_risk(constant("2023-06-05", 100), 101, 1);
    near(ex.log_return, 0); near(ex.daily2, 0); near(*ex.rv5, 0);
    const auto gap = trading::measure_daily_risk(constant("2023-06-05", 101, 24), 100, 0);
    require(gap.rv15 && !gap.rv5 && !gap.rv1, "an absent 09:54 bar invalidates RV5 without filling it");
    near(gap.daily2, r*r);
    const auto nonendpoint = trading::measure_daily_risk(constant("2023-06-05", 101, 22), 100, 0);
    require(nonendpoint.rv5 && nonendpoint.rv15 && !nonendpoint.rv1, "partition availability depends on its own endpoints");
    const auto shortened = trading::measure_daily_risk(constant("2023-07-03", 101, -1, true), 100, 0);
    near(*shortened.rv5, r*r); near(*shortened.rv15, r*r);
    const trading::CalendarSession c{"2023-06-02", "09:30", "16:00"};
    std::vector<trading::MinuteBar> alternating;
    double previous = 100, expected = 0;
    for (int i = 0; i < 390; ++i) {
        const auto p = (i / 5) % 2 == 0 ? 101.0 : 99.0;
        alternating.emplace_back("SPY", c.open_utc() + std::chrono::minutes{i}, trading::Ohlc{100, 101, 99, p}, 1);
        if (i % 5 == 4) { const auto move = std::log(p / previous); expected += move*move; previous = p; }
    }
    const auto saw = trading::measure_daily_risk({c, std::move(alternating)}, 100, 0);
    near(*saw.rv5, expected); require(*saw.rv5 > saw.daily2, "intraday reversals contain risk absent from the squared daily endpoint");
    rejects([&] { static_cast<void>(trading::measure_daily_risk(session, 0, 0)); });
    rejects([&] { static_cast<void>(trading::measure_daily_risk(session, 100, -1)); });
    rejects([] { static_cast<void>(constant("2023-06-02", 100, 389)); });
    rejects([] { static_cast<void>(trading::risk_budget_weight(0)); });
    near(trading::risk_budget_weight(.01*.01/252), 1);
    near(trading::risk_budget_weight(.04/252), .5);
    const std::array<double, 2> returns{.01, -.01};
    near(trading::certainty_equivalent(returns), -.0756);
}
void holding() {
    const auto first = constant("2023-06-02", 100), next = constant("2023-06-05", 99);
    for (const int delay : {1, 2}) {
        const auto result = trading::measure_holding_risk(first, next, 1, delay);
        require(result.rv5.has_value(), "complete dividend-aware holding partition exists");
        near(result.endpoint2, 0); near(*result.rv5, 0);
        const auto gap = trading::measure_holding_risk(constant("2023-06-02", 100, 24), next, 1, delay);
        require(!gap.rv5, "missing future label remains missing"); near(gap.endpoint2, 0);
    }
    rejects([&] { static_cast<void>(trading::measure_holding_risk(first, next, 1, 0)); });
    rejects([&] { static_cast<void>(trading::measure_holding_risk(next, first, 1, 1)); });
}
} // namespace
int main() {
    try { measurements(); holding(); std::cout << "Risk measurement contracts passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "Risk measurement contract failure: " << e.what() << '\n'; return 1; }
}
