#include "minute_bar.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// Synthetic fixtures exercise data contracts, not historical price behaviour.
const std::chrono::sys_seconds bar_start =
    std::chrono::sys_days{std::chrono::year{2023} / 1 / 3} +
    std::chrono::hours{14} + std::chrono::minutes{30};

void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string{description});
    }
}

template <typename Operation>
void expect_invalid(Operation operation, std::string_view description) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string{description});
}

void test_valid_bars() {
    const trading::MinuteBar bar{"SPY", bar_start, {100.0, 102.0, 99.0, 101.0}, 1000.5};
    require(bar.symbol() == "SPY", "symbol must be retained");
    require(bar.start() == bar_start, "UTC interval start must be retained");
    require(bar.prices().open == 100.0 && bar.prices().high == 102.0 &&
                bar.prices().low == 99.0 && bar.prices().close == 101.0,
            "OHLC values must be retained without clipping or rounding");
    require(bar.volume() == 1000.5, "fractional share volume must be retained");

    const trading::MinuteBar flat{"BRK.B", bar_start, {100.0, 100.0, 100.0, 100.0}, 0.0};
    require(flat.volume() == 0.0, "zero volume is valid at the individual-bar boundary");
    require(flat.symbol() == "BRK.B", "a symbol containing a period must be accepted");

    const trading::MinuteBar boundary{"SPY", bar_start, {99.0, 102.0, 99.0, 102.0}, 1.0};
    require(boundary.prices().open == boundary.prices().low &&
                boundary.prices().close == boundary.prices().high,
            "open and close may equal the interval extrema");
}

void test_symbols_and_time() {
    for (const std::string symbol : {"", " ", " SPY", "SPY ", "SP\tY", "SPY\n"}) {
        expect_invalid([&] {
            static_cast<void>(trading::MinuteBar{symbol, bar_start, {100.0, 102.0, 99.0, 101.0}, 1.0});
        }, "empty symbols, whitespace and control characters must be rejected");
    }
    expect_invalid([] {
        static_cast<void>(trading::MinuteBar{
            "SPY", bar_start + std::chrono::seconds{1}, {100.0, 102.0, 99.0, 101.0}, 1.0});
    }, "an intraminute timestamp must be rejected");
}

void test_invalid_prices() {
    const std::array invalid_values{
        0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    for (const double invalid : invalid_values) {
        const std::array cases{
            trading::Ohlc{invalid, 102.0, 99.0, 101.0},
            trading::Ohlc{100.0, invalid, 99.0, 101.0},
            trading::Ohlc{100.0, 102.0, invalid, 101.0},
            trading::Ohlc{100.0, 102.0, 99.0, invalid}};
        for (const auto prices : cases) {
            expect_invalid([&] {
                static_cast<void>(trading::MinuteBar{"SPY", bar_start, prices, 1.0});
            }, "each OHLC price must be finite and strictly positive");
        }
    }

    const std::array inconsistent_ranges{
        trading::Ohlc{100.0, 99.0, 101.0, 100.0},
        trading::Ohlc{98.0, 102.0, 99.0, 101.0},
        trading::Ohlc{103.0, 102.0, 99.0, 101.0},
        trading::Ohlc{100.0, 102.0, 99.0, 98.0},
        trading::Ohlc{100.0, 102.0, 99.0, 103.0}};
    for (const auto prices : inconsistent_ranges) {
        expect_invalid([&] {
            static_cast<void>(trading::MinuteBar{"SPY", bar_start, prices, 1.0});
        }, "inconsistent OHLC ranges must be rejected");
    }
}

void test_invalid_volume() {
    const std::array invalid_values{
        -1.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const double volume : invalid_values) {
        expect_invalid([&] {
            static_cast<void>(trading::MinuteBar{
                "SPY", bar_start, {100.0, 102.0, 99.0, 101.0}, volume});
        }, "negative or non-finite volume must be rejected");
    }
}

} // namespace

int main() {
    try {
        test_valid_bars();
        test_symbols_and_time();
        test_invalid_prices();
        test_invalid_volume();
        std::cout << "Minute-bar contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Minute-bar contract failure: " << error.what() << '\n';
        return 1;
    }
}
