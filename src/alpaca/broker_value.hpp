#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace trading {

// Exact bounded decimal text. Integer projection is explicit and never rounds.
class BrokerDecimal {
public:
    explicit BrokerDecimal(std::string_view text);
    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] std::int64_t scaled_integer(unsigned decimal_places) const;
    [[nodiscard]] std::strong_ordering operator<=>(const BrokerDecimal& other) const;
    bool operator==(const BrokerDecimal& other) const = default;

private:
    std::string value_;
};

// Separate from minute-bar timestamps: broker times retain nanoseconds/offsets.
class BrokerTimestamp {
public:
    explicit BrokerTimestamp(std::string_view rfc3339);
    [[nodiscard]] std::chrono::sys_time<std::chrono::nanoseconds> value() const noexcept;
    [[nodiscard]] const std::string& text() const noexcept;
    bool operator==(const BrokerTimestamp& other) const noexcept;

private:
    std::chrono::sys_time<std::chrono::nanoseconds> value_;
    std::string original_;
};

} // namespace trading
