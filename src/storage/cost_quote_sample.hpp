#pragma once
#include "api_request_policy.hpp"
#include "cost_execution_plan.hpp"
#include <functional>

namespace trading {
// The registered quote test is shared by acquisition and observational diagnostics.
// Source clocks must first pass CostQuoteSample's interval and ordering checks.
[[nodiscard]] nlohmann::json classify_cost_quote(const nlohmann::json &quote,
                                                 std::chrono::sys_seconds reference);
class CostQuoteSample {
  public:
    using Fetch =
        std::function<MarketDataResponse(const HistoricalTickRequest &, const std::optional<std::string> &)>;
    using WindowObserver = std::function<void(const CostQuoteWindow &, const nlohmann::json &,
                                              const std::vector<nlohmann::json> &)>;
    CostQuoteSample(std::filesystem::path directory, CostQuotePlan plan);
    [[nodiscard]] nlohmann::json run(bool acquire, const Fetch &fetch, ApiRequestPolicy::Wait wait,
                                     const std::function<void(const std::string &)> &progress = {},
                                     const WindowObserver &observer = {}) const;

  private:
    std::filesystem::path directory_;
    CostQuotePlan plan_;
};
} // namespace trading
