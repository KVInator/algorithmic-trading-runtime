#include "historical_tick_request.hpp"
#include "market_data_query.hpp"
#include "utc_timestamp.hpp"
#include <stdexcept>
#include <utility>

namespace trading {
HistoricalTickRequest::HistoricalTickRequest(HistoricalTickKind kind, std::string symbol,
    std::chrono::sys_seconds start, std::chrono::sys_seconds end)
    : kind_{kind}, symbol_{std::move(symbol)}, start_{start}, end_{end} {
    validate_equity_symbol(symbol_);
    const auto first = format_utc_seconds(start_);
    const auto last = format_utc_seconds(end_);
    if ((kind_ != HistoricalTickKind::quotes && kind_ != HistoricalTickKind::trades) ||
        first < "2016-01-01" || last >= "2026-01-01" || end_ <= start_ ||
        end_ - start_ > std::chrono::minutes{1}) {
        throw std::invalid_argument("tick capability requests require a 2016-2025 window of at most one minute");
    }
}
std::string HistoricalTickRequest::path(const std::optional<std::string>& token) const {
    return std::string{"/v2/stocks/"} + (kind_ == HistoricalTickKind::quotes ? "quotes" : "trades") +
        "?symbols=" + encode_query_value(symbol_) + "&start=" + encode_query_value(format_utc_seconds(start_)) +
        "&end=" + encode_query_value(format_utc_seconds(end_)) +
        "&limit=100&feed=sip&sort=asc&currency=USD&asof=-" + page_token_query(token);
}
} // namespace trading
