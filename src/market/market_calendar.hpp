#pragma once

#include "alpaca_bar_request.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

// Daytime New York sessions in the 2020-12-31 through 2025 historical interval.
// Uses the post-2007 US DST rule; it is not a general timezone database.
[[nodiscard]] std::chrono::sys_days parse_session_date(std::string_view date);

class CalendarRange {
  public:
    CalendarRange(std::string first, std::string last);
    [[nodiscard]] const std::string &first() const noexcept;
    [[nodiscard]] const std::string &last() const noexcept;
    [[nodiscard]] std::string path() const;

  private:
    std::string first_;
    std::string last_;
};

class CalendarSession {
  public:
    CalendarSession(std::string date, std::string open_local, std::string close_local);
    [[nodiscard]] const std::string &date() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds open_utc() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds close_utc() const noexcept;
    [[nodiscard]] bool regular() const noexcept;
    [[nodiscard]] HistoricalBarRequest bar_request() const;

  private:
    std::string date_;
    std::chrono::sys_seconds open_utc_;
    std::chrono::sys_seconds close_utc_;
    bool regular_;
};

[[nodiscard]] std::vector<CalendarSession> decode_market_calendar(std::string_view payload,
                                                                  const CalendarRange &range);

// Independent NYSE holiday/early-close reference, restricted to 2022-2023.
[[nodiscard]] std::vector<CalendarSession> development_calendar(const CalendarRange &range);

// NYSE 2021 holiday/early-close reference, plus the preceding 2020-12-31 close.
[[nodiscard]] std::vector<CalendarSession> risk_calibration_calendar(const CalendarRange &range);

} // namespace trading
