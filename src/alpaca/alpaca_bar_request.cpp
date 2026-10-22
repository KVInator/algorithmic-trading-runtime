#include "alpaca_bar_request.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace trading {
namespace {

std::string encode_query_value(std::string_view value) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char character : value) {
        if ((character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' || character == '~') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(hex[character >> 4]);
            result.push_back(hex[character & 0x0fU]);
        }
    }
    return result;
}

} // namespace

HistoricalBarRequest::HistoricalBarRequest(std::string symbol, std::chrono::sys_seconds start,
                                         std::chrono::sys_seconds end_exclusive,
                                         std::size_t page_size)
    : symbol_{std::move(symbol)}, start_{start}, end_exclusive_{end_exclusive}, page_size_{page_size} {
    if (symbol_.empty() || symbol_.size() > 32 ||
        !std::all_of(symbol_.begin(), symbol_.end(), [](unsigned char character) {
            return (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '.' || character == '-';
        })) {
        throw std::invalid_argument("request requires a single uppercase equity symbol (A-Z, 0-9, dot, hyphen)");
    }
    static_cast<void>(format_utc_seconds(start_));
    static_cast<void>(format_utc_seconds(end_exclusive_));
    if (start_.time_since_epoch().count() % 60 != 0 ||
        end_exclusive_.time_since_epoch().count() % 60 != 0 || end_exclusive_ <= start_ ||
        end_exclusive_ - start_ > std::chrono::hours{24}) {
        throw std::invalid_argument("request requires aligned UTC minutes and a positive interval of at most 24 hours");
    }
    if (page_size_ == 0 || page_size_ > 100) {
        throw std::invalid_argument("bounded historical requests require 1 to 100 bars per page");
    }
}

const std::string& HistoricalBarRequest::symbol() const noexcept { return symbol_; }
std::chrono::sys_seconds HistoricalBarRequest::start() const noexcept { return start_; }
std::chrono::sys_seconds HistoricalBarRequest::end_exclusive() const noexcept { return end_exclusive_; }
std::size_t HistoricalBarRequest::page_size() const noexcept { return page_size_; }
std::size_t HistoricalBarRequest::expected_minutes() const noexcept {
    return static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::minutes>(
        end_exclusive_ - start_).count());
}

std::string HistoricalBarRequest::path(const std::optional<std::string>& page_token) const {
    auto result = "/v2/stocks/bars?symbols=" + encode_query_value(symbol_) +
                  "&timeframe=1Min&start=" + encode_query_value(format_utc_seconds(start_)) +
                  "&end=" + encode_query_value(format_utc_seconds(end_exclusive_ - std::chrono::minutes{1})) +
                  "&limit=" + std::to_string(page_size_) +
                  "&feed=sip&adjustment=raw&sort=asc&currency=USD&asof=-";
    if (page_token) {
        if (page_token->empty() || page_token->size() > 4096) {
            throw std::invalid_argument("pagination token must contain 1 to 4096 bytes");
        }
        result += "&page_token=" + encode_query_value(*page_token);
    }
    return result;
}

} // namespace trading
