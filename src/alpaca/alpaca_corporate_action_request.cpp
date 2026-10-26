#include "alpaca_corporate_action_request.hpp"
#include "market_data_query.hpp"
#include "utc_timestamp.hpp"

#include <stdexcept>

namespace trading {

CorporateActionRequest::CorporateActionRequest(std::string first, std::string last)
    : CorporateActionRequest("SPY", std::move(first), std::move(last)) {
    if (first_ < "2021-01-01" || last_ > "2024-03-31") {
        throw std::invalid_argument("corporate-action acquisition requires process dates within 2021 through March 2024");
    }
}
CorporateActionRequest::CorporateActionRequest(std::string symbol, std::string first, std::string last)
    : symbol_{std::move(symbol)}, first_{std::move(first)}, last_{std::move(last)} {
    validate_equity_symbol(symbol_);
    static_cast<void>(parse_utc_seconds(first_ + "T00:00:00Z"));
    static_cast<void>(parse_utc_seconds(last_ + "T00:00:00Z"));
    if (first_.size() != 10 || last_.size() != 10 || first_ < "2016-01-01" ||
        last_ > "2026-03-31" || last_ < first_) {
        throw std::invalid_argument("explicit-symbol action requests require process dates within 2016 through March 2026");
    }
}
std::string CorporateActionRequest::path(const std::optional<std::string>& token) const {
    auto result = "/v1/corporate-actions?symbols=" + encode_query_value(symbol_) + "&start=" + first_ + "&end=" + last_ +
        "&limit=100&data_quality=complete&sort=asc";
    return result + page_token_query(token);
}

CorporateActionProbeRequest::CorporateActionProbeRequest(int year) : year_{year} {
    if (year < 2021 || year > 2023) { throw std::invalid_argument("corporate-action preflight is bounded to 2021-2023"); }
}
std::string CorporateActionProbeRequest::path() const {
    const auto prefix = std::to_string(year_);
    return "/v1/corporate-actions?symbols=SPY&start=" + prefix + "-01-01&end=" + prefix +
        "-12-31&limit=1000&data_quality=complete&sort=asc";
}

} // namespace trading
