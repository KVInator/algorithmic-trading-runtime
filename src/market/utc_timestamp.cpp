#include "utc_timestamp.hpp"

#include <charconv>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace trading {
namespace {

int component(std::string_view value, std::size_t offset, std::size_t length) {
    const auto first = value.data() + offset;
    const auto last = first + length;
    for (auto current = first; current != last; ++current) {
        if (*current < '0' || *current > '9') {
            throw std::invalid_argument("UTC timestamp contains a non-numeric component");
        }
    }
    int result = 0;
    const auto parsed = std::from_chars(first, last, result);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        throw std::invalid_argument("invalid UTC timestamp component");
    }
    return result;
}

} // namespace

std::chrono::sys_seconds parse_utc_seconds(std::string_view value) {
    if (value.size() < 20 || value.size() > 30 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        throw std::invalid_argument("UTC timestamp must use YYYY-MM-DDTHH:MM:SS[.000...]Z");
    }
    if (value.size() != 20) {
        if (value.size() < 22 || value[19] != '.') {
            throw std::invalid_argument("invalid UTC fractional-second representation");
        }
        for (std::size_t index = 20; index + 1 < value.size(); ++index) {
            if (value[index] != '0') {
                throw std::invalid_argument("non-zero fractional seconds cannot be represented exactly");
            }
        }
    }

    const int year = component(value, 0, 4);
    const int month = component(value, 5, 2);
    const int day = component(value, 8, 2);
    const int hour = component(value, 11, 2);
    const int minute = component(value, 14, 2);
    const int second = component(value, 17, 2);
    const std::chrono::year_month_day date{
        std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    if (year < 1 || !date.ok() || hour > 23 || minute > 59 || second > 59) {
        throw std::invalid_argument("UTC timestamp contains an invalid calendar date or time");
    }
    return std::chrono::sys_days{date} + std::chrono::hours{hour} +
           std::chrono::minutes{minute} + std::chrono::seconds{second};
}

std::string format_utc_seconds(std::chrono::sys_seconds value) {
    const auto minimum = std::chrono::sys_days{std::chrono::year{1} / 1 / 1};
    const auto maximum = std::chrono::sys_days{std::chrono::year{10000} / 1 / 1};
    if (value < minimum || value >= maximum) {
        throw std::invalid_argument("UTC timestamp requires a year between 0001 and 9999");
    }
    const auto date_start = std::chrono::floor<std::chrono::days>(value);
    const std::chrono::year_month_day date{date_start};
    const std::chrono::hh_mm_ss time{value - date_start};
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.day()) << 'T'
           << std::setw(2) << time.hours().count() << ':'
           << std::setw(2) << time.minutes().count() << ':'
           << std::setw(2) << time.seconds().count() << 'Z';
    return output.str();
}

} // namespace trading
