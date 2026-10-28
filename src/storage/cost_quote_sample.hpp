#pragma once
#include "api_request_policy.hpp"
#include "cost_execution_plan.hpp"
#include <functional>

namespace trading {
class CostQuoteSample {
  public:
    using Fetch =
        std::function<MarketDataResponse(const HistoricalTickRequest &, const std::optional<std::string> &)>;
    CostQuoteSample(std::filesystem::path directory, CostQuotePlan plan);
    [[nodiscard]] nlohmann::json run(bool acquire, const Fetch &fetch, ApiRequestPolicy::Wait wait,
                                     const std::function<void(const std::string &)> &progress = {}) const;

  private:
    std::filesystem::path directory_;
    CostQuotePlan plan_;
};
} // namespace trading
