#include "api_request_policy.hpp"

#include <charconv>
#include <stdexcept>
#include <utility>

namespace trading {

ApiRequestPolicy::ApiRequestPolicy(Wait wait, bool retry_server_errors)
    : wait_{std::move(wait)}, retry_server_errors_{retry_server_errors} {
    if (!wait_) { throw std::invalid_argument("API request policy requires an explicit wait function"); }
}

MarketDataResponse ApiRequestPolicy::get(const std::function<MarketDataResponse()>& request,
                                        const Observer& observer) const {
    if (!request) { throw std::invalid_argument("API request callback is missing"); }
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        wait_(std::chrono::milliseconds{750});
        auto response = request();
        if (observer) { observer(response); }
        const bool server_error = response.status == 500 || response.status == 502 ||
                                  response.status == 503 || response.status == 504;
        if (response.status != 429 && !(retry_server_errors_ && server_error)) { return response; }
        if (attempt == 2) { throw std::runtime_error("HTTP " + std::to_string(response.status) +
            " persisted after two retries; retained evidence is preserved"); }
        unsigned int seconds = 2;
        if (response.retry_after) {
            const auto& value = *response.retry_after;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                throw std::runtime_error("HTTP 429 has unsupported Retry-After; stop and retry the batch later");
            }
        }
        if (seconds > 30) { throw std::runtime_error("HTTP 429 requests a wait over 30 seconds; retry the batch later"); }
        wait_(std::chrono::seconds{seconds});
    }
    throw std::logic_error("unreachable API retry state");
}

} // namespace trading
