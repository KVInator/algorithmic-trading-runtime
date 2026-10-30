#include "market_calendar.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

std::chrono::minutes clock_minutes(std::string_view value) {
    if (value.size() == 8 && value.substr(5) == ":00") {
        value = value.substr(0, 5);
    }
    if (value.size() != 5 || value[2] != ':') {
        throw std::invalid_argument("calendar clock requires HH:MM or HH:MM:00");
    }
    auto component = [&](std::size_t index) {
        if (value[index] < '0' || value[index] > '9' || value[index + 1] < '0' ||
            value[index + 1] > '9') {
            throw std::invalid_argument("calendar clock contains a non-digit");
        }
        return (value[index] - '0') * 10 + value[index + 1] - '0';
    };
    const auto hour = component(0);
    const auto minute = component(3);
    if (hour > 23 || minute > 59) {
        throw std::invalid_argument("invalid calendar clock");
    }
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
    if (date.size() != 10) {
        throw std::invalid_argument("session date requires YYYY-MM-DD");
    }
    const auto value =
        std::chrono::floor<std::chrono::days>(parse_utc_seconds(std::string{date} + "T00:00:00Z"));
    const auto year = static_cast<int>(std::chrono::year_month_day{value}.year());
    if (year < 2016 || year > 2025) {
        throw std::invalid_argument("historical calendar conversion is bounded to 2016 through 2025");
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

const std::string &CalendarRange::first() const noexcept { return first_; }
const std::string &CalendarRange::last() const noexcept { return last_; }
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
        throw std::invalid_argument(
            "calendar session lies outside the supported 09:30-16:00 daytime window");
    }
    const auto offset = utc_offset_to_add(day);
    open_utc_ = day + open + offset;
    close_utc_ = day + close + offset;
    regular_ = open == std::chrono::minutes{570} && close == std::chrono::minutes{960};
}

const std::string &CalendarSession::date() const noexcept { return date_; }
std::chrono::sys_seconds CalendarSession::open_utc() const noexcept { return open_utc_; }
std::chrono::sys_seconds CalendarSession::close_utc() const noexcept { return close_utc_; }
bool CalendarSession::regular() const noexcept { return regular_; }
HistoricalBarRequest CalendarSession::bar_request() const { return bar_request("SPY"); }
HistoricalBarRequest CalendarSession::bar_request(std::string symbol) const {
    return {std::move(symbol), open_utc_, close_utc_, 100};
}

std::vector<CalendarSession> decode_market_calendar(std::string_view payload,
                                                    const CalendarRange &range) {
    const auto document = parse_json_document(payload);
    if (!document.is_array() || document.empty() || document.size() > 31) {
        throw std::invalid_argument("calendar response requires 1 to 31 session records");
    }
    std::vector<CalendarSession> result;
    for (const auto &record : document) {
        if (!record.is_object() || !record.contains("date") || !record.at("date").is_string() ||
            !record.contains("open") || !record.at("open").is_string() || !record.contains("close") ||
            !record.at("close").is_string()) {
            throw std::invalid_argument("calendar record requires date, open and close strings");
        }
        CalendarSession session{record.at("date").get<std::string>(),
                                record.at("open").get<std::string>(),
                                record.at("close").get<std::string>()};
        if (session.date() < range.first() || session.date() > range.last() ||
            (!result.empty() && session.date() <= result.back().date())) {
            throw std::invalid_argument(
                "calendar dates must be unique, ascending and within the requested range");
        }
        result.push_back(std::move(session));
    }
    return result;
}

namespace {
std::vector<CalendarSession> reference_sessions(const CalendarRange &range,
                                                std::span<const std::string_view> holidays,
                                                std::span<const std::string_view> short_dates) {
    std::vector<CalendarSession> result;
    for (auto day = parse_session_date(range.first()); day <= parse_session_date(range.last());
         day += std::chrono::days{1}) {
        const auto weekday = std::chrono::weekday{day};
        const auto date = format_utc_seconds(day).substr(0, 10);
        if (weekday == std::chrono::Saturday || weekday == std::chrono::Sunday ||
            std::find(holidays.begin(), holidays.end(), date) != holidays.end()) {
            continue;
        }
        const auto shortened =
            std::find(short_dates.begin(), short_dates.end(), date) != short_dates.end();
        result.emplace_back(date, "09:30", shortened ? "13:00" : "16:00");
    }
    return result;
}
} // namespace

std::vector<CalendarSession> development_calendar(const CalendarRange &range) {
    if (range.first() < "2022-01-01" || range.last() > "2023-12-31") {
        throw std::invalid_argument("development calendar is restricted to 2022-2023");
    }
    constexpr std::array<std::string_view, 19> holidays{
        "2022-01-17", "2022-02-21", "2022-04-15", "2022-05-30", "2022-06-20", "2022-07-04", "2022-09-05",
        "2022-11-24", "2022-12-26", "2023-01-02", "2023-01-16", "2023-02-20", "2023-04-07", "2023-05-29",
        "2023-06-19", "2023-07-04", "2023-09-04", "2023-11-23", "2023-12-25"};
    constexpr std::array<std::string_view, 3> short_dates{"2022-11-25", "2023-07-03", "2023-11-24"};
    return reference_sessions(range, holidays, short_dates);
}

std::vector<CalendarSession> risk_calibration_calendar(const CalendarRange &range) {
    if (range.first() < "2020-12-31" || range.last() > "2021-12-31") {
        throw std::invalid_argument(
            "risk calibration calendar is restricted to the 2020 boundary and 2021");
    }
    constexpr std::array<std::string_view, 9> holidays{"2021-01-01", "2021-01-18", "2021-02-15",
                                                       "2021-04-02", "2021-05-31", "2021-07-05",
                                                       "2021-09-06", "2021-11-25", "2021-12-24"};
    constexpr std::array<std::string_view, 1> short_dates{"2021-11-26"};
    return reference_sessions(range, holidays, short_dates);
}

std::vector<CalendarSession> extension_calendar(const CalendarRange &range) {
    const auto first = parse_session_date(range.first()), finish = parse_session_date(range.last());
    const int first_year = static_cast<int>(std::chrono::year_month_day{first}.year());
    const int last_year = static_cast<int>(std::chrono::year_month_day{finish}.year());
    std::set<std::chrono::sys_days> holidays, shortened;
    for (int y = first_year; y <= last_year; ++y) {
        const auto year = std::chrono::year{y};
        const auto observed = [](std::chrono::sys_days day) {
            const auto weekday = std::chrono::weekday{day};
            return day + (weekday == std::chrono::Saturday ? std::chrono::days{-1}
                          : weekday == std::chrono::Sunday ? std::chrono::days{1}
                                                           : std::chrono::days{0});
        };
        const auto new_year = std::chrono::sys_days{year / std::chrono::January / 1};
        // NYSE does not observe a Saturday New Year on the preceding Friday.
        if (std::chrono::weekday{new_year} != std::chrono::Saturday) {
            holidays.insert(observed(new_year));
        }
        holidays.insert(std::chrono::sys_days{year / std::chrono::January / std::chrono::Monday[3]});
        holidays.insert(std::chrono::sys_days{year / std::chrono::February / std::chrono::Monday[3]});
        const int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
        const int f = (b + 8) / 25, g = (b - f + 1) / 3;
        const int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4;
        const int l = (32 + 2 * e + 2 * i - h - k) % 7, m = (a + 11 * h + 22 * l) / 451;
        const int easter_month = (h + l - 7 * m + 114) / 31;
        const int easter_day = (h + l - 7 * m + 114) % 31 + 1;
        holidays.insert(
            std::chrono::sys_days{year / std::chrono::month{static_cast<unsigned>(easter_month)} /
                                  std::chrono::day{static_cast<unsigned>(easter_day)}} -
            std::chrono::days{2});
        holidays.insert(
            std::chrono::sys_days{year / std::chrono::May / std::chrono::Monday[std::chrono::last]});
        if (y >= 2022) {
            holidays.insert(observed(std::chrono::sys_days{year / std::chrono::June / 19}));
        }
        holidays.insert(observed(std::chrono::sys_days{year / std::chrono::July / 4}));
        holidays.insert(std::chrono::sys_days{year / std::chrono::September / std::chrono::Monday[1]});
        const auto thanksgiving =
            std::chrono::sys_days{year / std::chrono::November / std::chrono::Thursday[4]};
        holidays.insert(thanksgiving);
        holidays.insert(observed(std::chrono::sys_days{year / std::chrono::December / 25}));
        shortened.insert(thanksgiving + std::chrono::days{1});
        shortened.insert(std::chrono::sys_days{year / std::chrono::July / 3});
        shortened.insert(std::chrono::sys_days{year / std::chrono::December / 24});
    }
    holidays.insert(std::chrono::sys_days{std::chrono::year{2018} / 12 / 5});
    holidays.insert(std::chrono::sys_days{std::chrono::year{2025} / 1 / 9});
    std::vector<CalendarSession> result;
    for (auto day = first; day <= finish; day += std::chrono::days{1}) {
        if (std::chrono::weekday{day}.iso_encoding() >= 6 || holidays.contains(day)) {
            continue;
        }
        result.emplace_back(format_utc_seconds(day).substr(0, 10), "09:30",
                            shortened.contains(day) ? "13:00" : "16:00");
    }
    return result;
}

std::vector<CalendarSession> extension_calendar_years(int first, int last) {
    if (first < 2016 || last > 2025 || first > last)
        throw std::invalid_argument("extension calendar years outside registration");
    std::vector<CalendarSession> out;
    for (int y = first; y <= last; ++y)
        for (unsigned m = 1; m <= 12; ++m) {
            const auto end = std::chrono::year_month_day{std::chrono::year{y} / std::chrono::month{m} /
                                                         std::chrono::last};
            const auto id = std::to_string(y) + (m < 10 ? "-0" : "-") + std::to_string(m);
            const auto days = extension_calendar(
                CalendarRange{id + "-01", id + '-' + std::to_string(static_cast<unsigned>(end.day()))});
            out.insert(out.end(), days.begin(), days.end());
        }
    return out;
}
} // namespace trading
