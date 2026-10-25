#include "alpaca_corporate_action_request.hpp"
#include "utc_timestamp.hpp"

#include <stdexcept>

namespace trading {

CorporateActionRequest::CorporateActionRequest(std::string first, std::string last)
    : first_{std::move(first)}, last_{std::move(last)} {
    static_cast<void>(parse_utc_seconds(first_ + "T00:00:00Z"));
    static_cast<void>(parse_utc_seconds(last_ + "T00:00:00Z"));
    if (first_.size() != 10 || last_.size() != 10 || first_ < "2021-01-01" ||
        last_ > "2024-03-31" || last_ < first_) {
        throw std::invalid_argument("corporate-action acquisition requires process dates within 2021 through March 2024");
    }
}
std::string CorporateActionRequest::path(const std::optional<std::string>& token) const {
    auto result = "/v1/corporate-actions?symbols=SPY&start=" + first_ + "&end=" + last_ +
        "&limit=100&data_quality=complete&sort=asc";
    if (token) {
        if (token->empty() || token->size() > 4096) { throw std::invalid_argument("invalid action pagination token size"); }
        constexpr char hex[] = "0123456789ABCDEF";
        result += "&page_token=";
        for (const unsigned char c : *token) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~') { result += static_cast<char>(c); }
            else { result += '%'; result += hex[c >> 4]; result += hex[c & 15U]; }
        }
    }
    return result;
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
