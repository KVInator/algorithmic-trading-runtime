#include "alpaca_corporate_action_request.hpp"

#include <stdexcept>

namespace trading {

CorporateActionProbeRequest::CorporateActionProbeRequest(int year) : year_{year} {
    if (year < 2021 || year > 2023) { throw std::invalid_argument("corporate-action preflight is bounded to 2021-2023"); }
}
std::string CorporateActionProbeRequest::path() const {
    const auto prefix = std::to_string(year_);
    return "/v1/corporate-actions?symbols=SPY&start=" + prefix + "-01-01&end=" + prefix +
        "-12-31&limit=1000&data_quality=complete&sort=asc";
}

} // namespace trading
