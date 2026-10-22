#include "minute_bar.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

void require_positive_finite(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string{field} + " must be finite and positive");
    }
}

} // namespace

MinuteBar::MinuteBar(std::string symbol,
                     std::chrono::sys_seconds start,
                     Ohlc prices,
                     double volume)
    : symbol_{std::move(symbol)}, start_{start}, prices_{prices}, volume_{volume} {
    if (symbol_.empty() ||
        std::any_of(symbol_.begin(), symbol_.end(), [](unsigned char character) {
            return std::isspace(character) != 0 || std::iscntrl(character) != 0;
        })) {
        throw std::invalid_argument("symbol must be non-empty without whitespace or control characters");
    }

    if (start_ != std::chrono::floor<std::chrono::minutes>(start_)) {
        throw std::invalid_argument("bar start must be aligned to a UTC minute");
    }

    require_positive_finite(prices_.open, "open");
    require_positive_finite(prices_.high, "high");
    require_positive_finite(prices_.low, "low");
    require_positive_finite(prices_.close, "close");

    if (prices_.low > prices_.high) {
        throw std::invalid_argument("low must not exceed high");
    }
    if (prices_.open < prices_.low || prices_.open > prices_.high) {
        throw std::invalid_argument("open must lie within [low, high]");
    }
    if (prices_.close < prices_.low || prices_.close > prices_.high) {
        throw std::invalid_argument("close must lie within [low, high]");
    }
    if (!std::isfinite(volume_) || volume_ < 0.0) {
        throw std::invalid_argument("volume must be finite and non-negative");
    }
}

const std::string& MinuteBar::symbol() const noexcept {
    return symbol_;
}

std::chrono::sys_seconds MinuteBar::start() const noexcept {
    return start_;
}

const Ohlc& MinuteBar::prices() const noexcept {
    return prices_;
}

double MinuteBar::volume() const noexcept {
    return volume_;
}

} // namespace trading
