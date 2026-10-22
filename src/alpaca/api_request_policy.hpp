#pragma once

#include "alpaca_market_data_client.hpp"

#include <chrono>
#include <functional>

namespace trading {

// Adds a 750 ms pause before every request. Only HTTP 429 is retried, at most
// twice, honouring numeric Retry-After up to 30 seconds. Other statuses pass
// to the caller; transport exceptions are never converted into response data.
class ApiRequestPolicy {
public:
    using Wait = std::function<void(std::chrono::milliseconds)>;
    explicit ApiRequestPolicy(Wait wait);
    [[nodiscard]] MarketDataResponse get(const std::function<MarketDataResponse()>& request) const;
private:
    Wait wait_;
};

} // namespace trading
