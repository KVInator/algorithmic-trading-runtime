#pragma once

#include <chrono>
#include <string>

namespace trading {

struct Ohlc {
    double open;
    double high;
    double low;
    double close;
};

// A one-minute equity bar. Prices are quote-currency units per share;
// volume is in shares, and start is the beginning of the interval in UTC.
class MinuteBar {
public:
    MinuteBar(std::string symbol,
              std::chrono::sys_seconds start,
              Ohlc prices,
              double volume);

    [[nodiscard]] const std::string& symbol() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds start() const noexcept;
    [[nodiscard]] const Ohlc& prices() const noexcept;
    [[nodiscard]] double volume() const noexcept;

private:
    std::string symbol_;
    std::chrono::sys_seconds start_;
    Ohlc prices_;
    double volume_;
};

} // namespace trading
