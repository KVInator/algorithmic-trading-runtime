#include "market_calendar.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"

#include <stdexcept>
#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace trading {
namespace {

std::chrono::minutes clock_minutes(std::string_view value) {
    if (value.size() == 8 && value.substr(5) == ":00") { value = value.substr(0, 5); }
    if (value.size() != 5 || value[2] != ':') {
        throw std::invalid_argument("calendar clock requires HH:MM or HH:MM:00");
    }
    auto component = [&](std::size_t index) {
        if (value[index] < '0' || value[index] > '9' || value[index + 1] < '0' || value[index + 1] > '9') {
            throw std::invalid_argument("calendar clock contains a non-digit");
        }
        return (value[index] - '0') * 10 + value[index + 1] - '0';
    };
    const auto hour = component(0);
    const auto minute = component(3);
    if (hour > 23 || minute > 59) { throw std::invalid_argument("invalid calendar clock"); }
    return std::chrono::minutes{hour * 60 + minute};
}

std::chrono::hours utc_offset_to_add(std::chrono::sys_days date) {
    const auto year = std::chrono::year_month_day{date}.year();
    const auto start = std::chrono::sys_days{year / std::chrono::March / std::chrono::Sunday[2]};
    const auto finish = std::chrono::sys_days{year / std::chrono::November / std::chrono::Sunday[1]};
    return date >= start && date < finish ? std::chrono::hours{4} : std::chrono::hours{5};
}

} // namespace

std::chrono::sys_days parse_session_date(std::string_view date) {
    if (date.size() != 10) { throw std::invalid_argument("session date requires YYYY-MM-DD"); }
    const auto value = std::chrono::floor<std::chrono::days>(
        parse_utc_seconds(std::string{date} + "T00:00:00Z"));
    const auto year = static_cast<int>(std::chrono::year_month_day{value}.year());
    if (date < "2020-12-31" || year > 2025) {
        throw std::invalid_argument("historical calendar conversion is bounded to 2020-12-31 through 2025");
    }
    return value;
}

CalendarRange::CalendarRange(std::string first, std::string last)
    : first_{std::move(first)}, last_{std::move(last)} {
    const auto start = parse_session_date(first_);
    const auto end = parse_session_date(last_);
    if (end < start || end - start > std::chrono::days{30}) {
        throw std::invalid_argument("calendar range requires 1 to 31 inclusive days");
    }
}

const std::string& CalendarRange::first() const noexcept { return first_; }
const std::string& CalendarRange::last() const noexcept { return last_; }
std::string CalendarRange::path() const {
    return "/v2/calendar?start=" + first_ + "&end=" + last_ + "&date_type=TRADING";
}

CalendarSession::CalendarSession(std::string date, std::string open_local, std::string close_local)
    : date_{std::move(date)}, regular_{false} {
    const auto day = parse_session_date(date_);
    const auto weekday = std::chrono::weekday{day};
    if (weekday == std::chrono::Saturday || weekday == std::chrono::Sunday) {
        throw std::invalid_argument("equity calendar returned a weekend session");
    }
    const auto open = clock_minutes(open_local);
    const auto close = clock_minutes(close_local);
    if (open < std::chrono::minutes{570} || close > std::chrono::minutes{960} || close <= open) {
        throw std::invalid_argument("calendar session lies outside the supported 09:30-16:00 daytime window");
    }
    const auto offset = utc_offset_to_add(day);
    open_utc_ = day + open + offset;
    close_utc_ = day + close + offset;
    regular_ = open == std::chrono::minutes{570} && close == std::chrono::minutes{960};
}

const std::string& CalendarSession::date() const noexcept { return date_; }
std::chrono::sys_seconds CalendarSession::open_utc() const noexcept { return open_utc_; }
std::chrono::sys_seconds CalendarSession::close_utc() const noexcept { return close_utc_; }
bool CalendarSession::regular() const noexcept { return regular_; }
HistoricalBarRequest CalendarSession::bar_request() const { return {"SPY", open_utc_, close_utc_, 100}; }

std::vector<CalendarSession> decode_market_calendar(std::string_view payload, const CalendarRange& range) {
    const auto document = parse_json_document(payload);
    if (!document.is_array() || document.empty() || document.size() > 31) {
        throw std::invalid_argument("calendar response requires 1 to 31 session records");
    }
    std::vector<CalendarSession> result;
    for (const auto& record : document) {
        if (!record.is_object() || !record.contains("date") || !record.at("date").is_string() ||
            !record.contains("open") || !record.at("open").is_string() ||
            !record.contains("close") || !record.at("close").is_string()) {
            throw std::invalid_argument("calendar record requires date, open and close strings");
        }
        CalendarSession session{record.at("date").get<std::string>(), record.at("open").get<std::string>(),
                                 record.at("close").get<std::string>()};
        if (session.date() < range.first() || session.date() > range.last() ||
            (!result.empty() && session.date() <= result.back().date())) {
            throw std::invalid_argument("calendar dates must be unique, ascending and within the requested range");
        }
        result.push_back(std::move(session));
    }
    return result;
}

namespace {
std::vector<CalendarSession> reference_sessions(const CalendarRange& range,
    std::span<const std::string_view> holidays, std::span<const std::string_view> short_dates) {
    std::vector<CalendarSession> result;
    for (auto day = parse_session_date(range.first()); day <= parse_session_date(range.last()); day += std::chrono::days{1}) {
        const auto weekday = std::chrono::weekday{day};
        const auto date = format_utc_seconds(day).substr(0, 10);
        if (weekday == std::chrono::Saturday || weekday == std::chrono::Sunday ||
            std::find(holidays.begin(), holidays.end(), date) != holidays.end()) { continue; }
        const auto shortened = std::find(short_dates.begin(), short_dates.end(), date) != short_dates.end();
        result.emplace_back(date, "09:30", shortened ? "13:00" : "16:00");
    }
    return result;
}
} // namespace

std::vector<CalendarSession> development_calendar(const CalendarRange& range) {
    if (range.first() < "2022-01-01" || range.last() > "2023-12-31") {
        throw std::invalid_argument("development calendar is restricted to 2022-2023");
    }
    constexpr std::array<std::string_view, 19> holidays{
        "2022-01-17", "2022-02-21", "2022-04-15", "2022-05-30", "2022-06-20",
        "2022-07-04", "2022-09-05", "2022-11-24", "2022-12-26",
        "2023-01-02", "2023-01-16", "2023-02-20", "2023-04-07", "2023-05-29",
        "2023-06-19", "2023-07-04", "2023-09-04", "2023-11-23", "2023-12-25"};
    constexpr std::array<std::string_view, 3> short_dates{"2022-11-25", "2023-07-03", "2023-11-24"};
    return reference_sessions(range, holidays, short_dates);
}

std::vector<CalendarSession> risk_calibration_calendar(const CalendarRange& range) {
    if (range.first() < "2020-12-31" || range.last() > "2021-12-31") {
        throw std::invalid_argument("risk calibration calendar is restricted to the 2020 boundary and 2021");
    }
    constexpr std::array<std::string_view, 9> holidays{
        "2021-01-01", "2021-01-18", "2021-02-15", "2021-04-02", "2021-05-31",
        "2021-07-05", "2021-09-06", "2021-11-25", "2021-12-24"};
    constexpr std::array<std::string_view, 1> short_dates{"2021-11-26"};
    return reference_sessions(range, holidays, short_dates);
}

} // namespace trading
