#pragma once

#include "alpaca_market_data_client.hpp"

#include <chrono>
#include <functional>

namespace trading {

// Adds a 750 ms pause before every request. HTTP 429 is retried at most twice,
// honouring numeric Retry-After up to 30 seconds. Optional transient
// server retries are explicit; legacy callers keep their existing behaviour.
// Other statuses pass
// to the caller; transport exceptions are never converted into response data.
class ApiRequestPolicy {
public:
    using Wait = std::function<void(std::chrono::milliseconds)>;
    using Observer = std::function<void(const MarketDataResponse&)>;
    explicit ApiRequestPolicy(Wait wait, bool retry_server_errors = false);
    [[nodiscard]] MarketDataResponse get(const std::function<MarketDataResponse()>& request,
                                        const Observer& observer = {}) const;
private:
    Wait wait_;
    bool retry_server_errors_;
};

} // namespace trading
