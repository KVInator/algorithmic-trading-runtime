#pragma once
#include "extension_contract.hpp"
#include "panel_replay.hpp"

namespace trading {
struct CostDelayOutcome {
    double cost_bps, gross_return{}, net_return{}, traded_notional{}, cost_paid{};
};
struct CostDelayPath {
    std::string policy, reason;
    int delay_minutes;
    bool resolved;
    std::vector<ScheduledRoundTrip> trades;
    std::vector<CostDelayOutcome> outcomes;
    std::int64_t minutes_held{};
};

// Freeze M3 decisions, including its primary entry hurdle. Only observed
// execution opens and all-in charges change. Fractional notional references
// are distinct from the whole-share shared-capital/broker accounts.
class CostDelayStudy {
  public:
    explicit CostDelayStudy(const ExtensionStudyContract &contract);
    [[nodiscard]] const std::vector<double> &costs() const noexcept;
    [[nodiscard]] const std::vector<int> &delays() const noexcept;
    [[nodiscard]] CostDelayPath replay(const PanelPolicyReplay &policy, const HistoricalBarRequest &request,
                                       std::span<const MinuteBar> bars, int delay_minutes) const;
    [[nodiscard]] std::vector<CostDelayPath> evaluate(const PanelReplayDay &day,
                                                      const SessionDownload &session) const;

  private:
    std::vector<double> costs_;
    std::vector<int> delays_;
};
} // namespace trading
