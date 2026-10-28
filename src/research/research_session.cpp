#include "research_session.hpp"
#include "market_calendar.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace trading {
ResearchSessionView::ResearchSessionView(const HistoricalBarRequest& request, std::span<const MinuteBar> bars)
    : request_(request), bars_(bars) { static_cast<void>(audit_session(request_, bars_)); }
const MinuteBar* ResearchSessionView::at(std::size_t minute) const {
    if (minute >= request_.expected_minutes()) { throw std::invalid_argument("observation clock is outside the session"); }
    const auto clock = request_.start() + std::chrono::minutes{static_cast<int>(minute)};
    const auto found = std::lower_bound(bars_.begin(), bars_.end(), clock, [](const auto& bar, const auto& at) { return bar.start() < at; });
    return found != bars_.end() && found->start() == clock ? &*found : nullptr;
}
std::span<const MinuteBar> ResearchSessionView::before(std::chrono::sys_seconds cutoff) const {
    if (cutoff <= request_.start() || cutoff > request_.end_exclusive() || cutoff != std::chrono::floor<std::chrono::minutes>(cutoff)) { throw std::invalid_argument("invalid completed-prefix cutoff"); }
    const auto end = std::lower_bound(bars_.begin(), bars_.end(), cutoff, [](const auto& bar, const auto& at) { return bar.start() < at; });
    return bars_.first(static_cast<std::size_t>(end - bars_.begin()));
}
void require_panel_regular_session(const HistoricalBarRequest& request) {
    constexpr std::array<std::string_view, 5> symbols{"SPY", "QQQ", "IWM", "TLT", "GLD"};
    const auto day = format_utc_seconds(request.start()).substr(0, 10);
    if (day >= "2024-01-01" || std::find(symbols.begin(), symbols.end(), request.symbol()) == symbols.end()) {
        throw std::invalid_argument("panel strategy access requires a registered symbol and a pre-2024 date");
    }
    const auto calendar = extension_calendar(CalendarRange{day, day});
    if (calendar.size() != 1 || !calendar.front().regular() || request.start() != calendar.front().open_utc() ||
        request.end_exclusive() != calendar.front().close_utc()) {
        throw std::invalid_argument("panel strategy requires a calendar-verified regular session");
    }
}
} // namespace trading
