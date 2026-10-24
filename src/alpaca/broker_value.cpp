#include "broker_value.hpp"

#include "utc_timestamp.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
bool digit(char c) {
    return c >= '0' && c <= '9';
}

std::pair<std::string_view, std::string_view> parts(std::string_view text) {
    if (text.starts_with('-')) {
        text.remove_prefix(1);
    }
    const auto point = text.find('.');
    return {text.substr(0, point), point == text.npos ? std::string_view{} : text.substr(point + 1)};
}
} // namespace

BrokerDecimal::BrokerDecimal(std::string_view text) {
    if (text.empty() || text.size() > 66) {
        throw std::invalid_argument("broker decimal length is unsupported");
    }
    const bool negative = text.front() == '-';
    if (negative) {
        text.remove_prefix(1);
    }
    if (text.empty() || !digit(text.front())) {
        throw std::invalid_argument("invalid broker decimal sign or leading digit");
    }
    const auto [whole, fraction] = parts(text);
    if (whole.empty() || whole.size() > 32 || fraction.size() > 32 ||
        (text.find('.') != text.npos && fraction.empty()) ||
        !std::all_of(whole.begin(), whole.end(), digit) ||
        !std::all_of(fraction.begin(), fraction.end(), digit)) {
        throw std::invalid_argument("broker decimal requires bounded plain decimal digits");
    }
    const auto first = whole.find_first_not_of('0');
    value_ = first == whole.npos ? "0" : std::string{whole.substr(first)};
    const auto last = fraction.find_last_not_of('0');
    if (last != fraction.npos) {
        value_ += '.' + std::string{fraction.substr(0, last + 1)};
    }
    if (negative && value_ != "0") {
        value_.insert(value_.begin(), '-');
    }
}

const std::string& BrokerDecimal::text() const noexcept {
    return value_;
}

std::int64_t BrokerDecimal::scaled_integer(unsigned places) const {
    if (places > 18) {
        throw std::invalid_argument("integer decimal scale exceeds eighteen places");
    }
    const auto [whole, fraction] = parts(value_);
    if (fraction.size() > places) {
        throw std::invalid_argument("broker decimal cannot be projected without rounding");
    }
    const bool negative = value_.front() == '-';
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto bound = maximum + (negative ? 1U : 0U);
    std::uint64_t amount = 0;
    const auto append = [&](char c) {
        const auto n = static_cast<unsigned>(c - '0');
        if (amount > (bound - n) / 10) {
            throw std::invalid_argument("broker decimal integer projection overflow");
        }
        amount = amount * 10 + n;
    };
    for (const auto c : whole) {
        append(c);
    }
    for (const auto c : fraction) {
        append(c);
    }
    for (auto i = fraction.size(); i < places; ++i) {
        append('0');
    }
    if (negative && amount == maximum + 1) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return negative ? -static_cast<std::int64_t>(amount) : static_cast<std::int64_t>(amount);
}

std::strong_ordering BrokerDecimal::operator<=>(const BrokerDecimal& other) const {
    const bool negative = value_.front() == '-', other_negative = other.value_.front() == '-';
    if (negative != other_negative) {
        return negative ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    const auto [whole, fraction] = parts(value_);
    const auto [other_whole, other_fraction] = parts(other.value_);
    auto order = whole.size() <=> other_whole.size();
    if (order == 0) {
        order = whole <=> other_whole;
    }
    if (order == 0) {
        for (std::size_t i = 0; i < std::max(fraction.size(), other_fraction.size()); ++i) {
            const auto a = i < fraction.size() ? fraction[i] : '0';
            const auto b = i < other_fraction.size() ? other_fraction[i] : '0';
            order = a <=> b;
            if (order != 0) {
                break;
            }
        }
    }
    if (!negative || order == 0) {
        return order;
    }
    return order < 0 ? std::strong_ordering::greater : std::strong_ordering::less;
}

BrokerTimestamp::BrokerTimestamp(std::string_view text) {
    if (text.size() < 20 || text.size() > 35) {
        throw std::invalid_argument("broker timestamp length is unsupported");
    }
    // Keep nanosecond conversion inside the range representable by int64.
    const auto date = parse_utc_seconds(std::string{text.substr(0, 19)} + 'Z');
    if (date < std::chrono::sys_days{std::chrono::year{1970} / 1 / 1} ||
        date >= std::chrono::sys_days{std::chrono::year{2200} / 1 / 1}) {
        throw std::invalid_argument("broker timestamp must lie in 1970 through 2199");
    }
    std::size_t index = 19;
    std::int64_t nanos = 0;
    if (text[index] == '.') {
        const auto first = ++index;
        while (index < text.size() && digit(text[index])) {
            if (index - first == 9) {
                throw std::invalid_argument("broker timestamp exceeds nanosecond precision");
            }
            nanos = nanos * 10 + static_cast<int>(text[index++] - '0');
        }
        if (index == first) {
            throw std::invalid_argument("empty broker timestamp fraction");
        }
        for (auto n = index - first; n < 9; ++n) {
            nanos *= 10;
        }
    }
    int offset = 0;
    if (text.substr(index) != "Z") {
        const auto zone = text.substr(index);
        if (zone.size() != 6 || (zone[0] != '+' && zone[0] != '-') || zone[3] != ':' || !digit(zone[1]) ||
            !digit(zone[2]) || !digit(zone[4]) || !digit(zone[5])) {
            throw std::invalid_argument("invalid broker UTC offset");
        }
        const int hours = (zone[1] - '0') * 10 + zone[2] - '0',
                  minutes = (zone[4] - '0') * 10 + zone[5] - '0';
        if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0) || zone == "-00:00") {
            throw std::invalid_argument("unsupported or unknown broker UTC offset");
        }
        offset = (hours * 60 + minutes) * (zone[0] == '-' ? -1 : 1);
    }
    value_ = date - std::chrono::minutes{offset} + std::chrono::nanoseconds{nanos};
    original_ = text;
}

std::chrono::sys_time<std::chrono::nanoseconds> BrokerTimestamp::value() const noexcept {
    return value_;
}

const std::string& BrokerTimestamp::text() const noexcept {
    return original_;
}

bool BrokerTimestamp::operator==(const BrokerTimestamp& other) const noexcept {
    return value_ == other.value_;
}
} // namespace trading
