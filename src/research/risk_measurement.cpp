#include "daily_risk_study.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
double positive(double value) {
    if (!std::isfinite(value) || value <= 0) { throw std::invalid_argument("risk reference must be finite and positive"); }
    return value;
}
std::optional<double> realized_variation(const RiskSessionReferences& session,
    double previous_close, double dividend, int step) {
    const auto opening = session.open_at(session.calendar().open_utc());
    if (!opening) { return {}; }
    double last = positive(previous_close);
    double squared = 0;
    auto add = [&](double raw) {
        const double next = positive(raw + dividend);
        const double move = std::log(next / last);
        squared += move * move;
        last = next;
    };
    add(*opening);
    for (auto end = session.calendar().open_utc() + std::chrono::minutes{step};
         end <= session.calendar().close_utc(); end += std::chrono::minutes{step}) {
        const auto price = session.close_at(end - std::chrono::minutes{1});
        if (!price) { return {}; }
        add(*price);
    }
    if (!std::isfinite(squared)) { throw std::overflow_error("realized risk exceeds the finite range"); }
    return squared;
}
} // namespace

RiskSessionReferences::RiskSessionReferences(CalendarSession calendar, std::vector<MinuteBar> bars)
    : calendar_{std::move(calendar)}, bars_{std::move(bars)} {
    static_cast<void>(audit_session(calendar_.bar_request(), bars_));
    static_cast<void>(close());
}
const CalendarSession& RiskSessionReferences::calendar() const noexcept { return calendar_; }
std::optional<double> RiskSessionReferences::open_at(std::chrono::sys_seconds at) const {
    const auto it = std::lower_bound(bars_.begin(), bars_.end(), at, [](const auto& bar, const auto& t) { return bar.start() < t; });
    return it != bars_.end() && it->start() == at ? std::optional<double>{it->prices().open} : std::nullopt;
}
std::optional<double> RiskSessionReferences::close_at(std::chrono::sys_seconds at) const {
    const auto it = std::lower_bound(bars_.begin(), bars_.end(), at, [](const auto& bar, const auto& t) { return bar.start() < t; });
    return it != bars_.end() && it->start() == at ? std::optional<double>{it->prices().close} : std::nullopt;
}
double RiskSessionReferences::close() const {
    const auto result = close_at(calendar_.close_utc() - std::chrono::minutes{1});
    if (!result) { throw std::invalid_argument("missing closing reference on " + calendar_.date()); }
    return *result;
}
DailyRiskMeasurement measure_daily_risk(const RiskSessionReferences& session, double previous_close, double dividend) {
    positive(previous_close);
    if (!std::isfinite(dividend) || dividend < 0) { throw std::invalid_argument("invalid ex-date USD/share amount"); }
    const auto move = std::log(positive(session.close() + dividend) / previous_close);
    if (!std::isfinite(move)) { throw std::overflow_error("daily log return exceeds the finite range"); }
    return {session.calendar().date(), move, move * move,
        realized_variation(session, previous_close, dividend, 1), realized_variation(session, previous_close, dividend, 5),
        realized_variation(session, previous_close, dividend, 15)};
}
HoldingRiskMeasurement measure_holding_risk(const RiskSessionReferences& first, const RiskSessionReferences& next,
    double dividend, int delay) {
    if ((delay != 1 && delay != 2) || next.calendar().date() <= first.calendar().date() || !std::isfinite(dividend) || dividend < 0) {
        throw std::invalid_argument("invalid holding-risk interval");
    }
    const auto start = first.calendar().open_utc() + std::chrono::minutes{delay};
    const auto end = next.calendar().open_utc() + std::chrono::minutes{delay};
    const auto entry = first.open_at(start), exit = next.open_at(end);
    if (!entry || !exit) { throw std::invalid_argument("missing morning execution reference"); }
    const double endpoint = std::log(positive(*exit + dividend) / positive(*entry));
    double last = *entry, squared = 0;
    auto add = [&](double value) { const auto move = std::log(positive(value) / last); squared += move * move; last = value; };
    for (auto t = first.calendar().open_utc() + std::chrono::minutes{5}; t <= first.calendar().close_utc(); t += std::chrono::minutes{5}) {
        if (t <= start) { continue; }
        const auto raw = first.close_at(t - std::chrono::minutes{1});
        if (!raw) { return {endpoint * endpoint, {}}; }
        add(*raw);
    }
    const auto opening = next.open_at(next.calendar().open_utc());
    if (!opening) { throw std::invalid_argument("missing next opening account reference"); }
    add(*opening + dividend);
    for (auto t = next.calendar().open_utc() + std::chrono::minutes{5}; t < end; t += std::chrono::minutes{5}) {
        const auto raw = next.close_at(t - std::chrono::minutes{1});
        if (!raw) { return {endpoint * endpoint, {}}; }
        add(*raw + dividend);
    }
    add(*exit + dividend);
    if (!std::isfinite(squared)) { throw std::overflow_error("holding-risk outcome exceeds the finite range"); }
    return {endpoint * endpoint, squared};
}
double risk_budget_weight(double h) { return std::min(1.0, .10 / std::sqrt(252 * positive(h))); }

} // namespace trading
