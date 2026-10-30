#pragma once
#include "panel_replay_dataset.hpp"
#include <memory>

namespace trading {
// Continuous fractional risk ledgers and whole-share shared ledgers retain
// separate financial conventions. Both consume the same frozen M3 decisions.
// Single risk ledger shared by the primary study, secondary cases and overlays.
class PanelRiskLedger {
  public:
    PanelRiskLedger(std::string symbol, std::string policy, int delay = 1, double cost_bps = 1);
    ~PanelRiskLedger();
    [[nodiscard]] nlohmann::json append(const PanelReplayDay& day, const RiskSessionReferences& references,
        const nlohmann::json& actions, bool terminal);
    [[nodiscard]] nlohmann::json append_sized(const PanelReplayDay& day, const RiskSessionReferences& references,
        const nlohmann::json& actions, bool terminal, std::optional<double> target);
  private:
    class State;
    std::unique_ptr<State> state_;
};
class PanelAccountStudy {
  public:
    PanelAccountStudy(const ExtensionStudyContract &contract, nlohmann::json actions);
    ~PanelAccountStudy();
    [[nodiscard]] nlohmann::json plan() const;
    [[nodiscard]] nlohmann::json append(std::span<const PanelReplayDay> days,
                                        std::span<const SessionDownload> sessions,
                                        std::span<const std::optional<MinuteBar>> recovered, bool terminal);
    [[nodiscard]] nlohmann::json finish();

  private:
    class State;
    std::unique_ptr<State> state_;
};
} // namespace trading
