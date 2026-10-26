#include "alpaca_bar_request.hpp"
#include "market_data_query.hpp"
#include "utc_timestamp.hpp"

#include <stdexcept>
#include <utility>

namespace trading {
HistoricalBarRequest::HistoricalBarRequest(std::string symbol, std::chrono::sys_seconds start,
                                         std::chrono::sys_seconds end_exclusive,
                                         std::size_t page_size)
    : symbol_{std::move(symbol)}, start_{start}, end_exclusive_{end_exclusive}, page_size_{page_size} {
    validate_equity_symbol(symbol_);
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
    return result + page_token_query(page_token);
}

} // namespace trading
