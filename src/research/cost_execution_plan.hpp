#pragma once
#include "extension_contract.hpp"
#include "historical_tick_request.hpp"
#include <filesystem>
#include <vector>

namespace trading {
struct CostQuoteWindow {
    std::string id, symbol, date;
    int decision_minute, delay_minutes;
    HistoricalTickRequest request;
};
class CostQuotePlan {
  public:
    [[nodiscard]] static CostQuotePlan from_panel(const std::filesystem::path &dataset);
    [[nodiscard]] static CostQuotePlan constructed(std::vector<std::string> dates = {"2022-01-04"});
    [[nodiscard]] const nlohmann::json &document() const noexcept;
    [[nodiscard]] const std::vector<CostQuoteWindow> &windows() const noexcept;

  private:
    CostQuotePlan(std::vector<std::string> dates, bool synthetic, nlohmann::json evidence);
    nlohmann::json document_;
    std::vector<CostQuoteWindow> windows_;
};
struct ExecutionStressWorkload {
    std::string id, symbol, side, path{"steady"};
    int requested_shares{10}, displayed_shares{10}, spread_cents{2};
    int outbound_seconds{}, receipt_seconds{}, cancel_processing_seconds{1}, source_age_seconds{};
};
[[nodiscard]] std::vector<ExecutionStressWorkload> cost_execution_workloads();
} // namespace trading
