#include "minute_bar_csv.hpp"
#include "utc_timestamp.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace trading {
namespace {

constexpr std::string_view header = "symbol,timestamp_utc,open,high,low,close,volume_shares";
constexpr std::size_t maximum_rows = 1440;

void require_unquoted_symbol(std::string_view symbol) {
    if (symbol.find_first_of(",\"") != std::string_view::npos) {
        throw std::invalid_argument("bar CSV schema does not support comma or quote characters in symbols");
    }
}

std::string_view next_line(std::string_view text, std::size_t& offset) {
    const auto end = text.find('\n', offset);
    auto line = text.substr(offset, end == std::string_view::npos ? text.size() - offset : end - offset);
    offset = end == std::string_view::npos ? text.size() : end + 1;
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

std::array<std::string_view, 7> fields(std::string_view line) {
    std::array<std::string_view, 7> result{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto comma = line.find(',', start);
        if ((index + 1 < result.size() && comma == std::string_view::npos) ||
            (index + 1 == result.size() && comma != std::string_view::npos)) {
            throw std::invalid_argument("bar CSV row must contain exactly seven unquoted fields");
        }
        result[index] = line.substr(start, comma == std::string_view::npos ? line.size() - start : comma - start);
        start = comma == std::string_view::npos ? line.size() : comma + 1;
    }
    return result;
}

double number(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument("bar CSV numeric fields must not be empty");
    }
    double result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result,
                                        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument("bar CSV numeric field is invalid or outside the double range");
    }
    return result;
}

} // namespace

std::string format_minute_bars_csv(std::span<const MinuteBar> bars) {
    if (bars.size() > maximum_rows) {
        throw std::invalid_argument("single-session bar CSV exceeds 1440 rows");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << header << '\n';
    for (const auto& bar : bars) {
        require_unquoted_symbol(bar.symbol());
        const auto& prices = bar.prices();
        output << bar.symbol() << ',' << format_utc_seconds(bar.start()) << ','
               << prices.open << ',' << prices.high << ',' << prices.low << ','
               << prices.close << ',' << bar.volume() << '\n';
    }
    return output.str();
}

std::vector<MinuteBar> parse_minute_bars_csv(std::string_view payload) {
    if (payload.empty() || payload.size() > 1024U * 1024U) {
        throw std::invalid_argument("single-session bar CSV must contain 1 byte to 1 MiB");
    }
    std::size_t offset = 0;
    if (next_line(payload, offset) != header) {
        throw std::invalid_argument("bar CSV header does not match the seven-column schema");
    }
    std::vector<MinuteBar> bars;
    std::size_t line_number = 1;
    while (offset < payload.size()) {
        const auto line = next_line(payload, offset);
        ++line_number;
        if (bars.size() == maximum_rows) {
            throw std::invalid_argument("single-session bar CSV exceeds 1440 rows");
        }
        try {
            const auto row = fields(line);
            require_unquoted_symbol(row[0]);
            bars.emplace_back(std::string{row[0]}, parse_utc_seconds(row[1]),
                              Ohlc{number(row[2]), number(row[3]), number(row[4]), number(row[5])},
                              number(row[6]));
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("bar CSV line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    return bars;
}

} // namespace trading
