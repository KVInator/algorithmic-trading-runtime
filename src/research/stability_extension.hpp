#pragma once
#include "risk_match.hpp"
#include <memory>

namespace trading {
struct StabilityVariant {
    std::string id, family;
    PanelReplayParameters parameters;
};
[[nodiscard]] nlohmann::json replay_parameters_json(const PanelReplayParameters &parameters);
[[nodiscard]] std::vector<StabilityVariant> stability_variants(const ExtensionStudyContract &contract);
class StabilityExtensionStudy {
  public:
    StabilityExtensionStudy(const ExtensionStudyContract &contract, PanelInputPolicy policy,
                            nlohmann::json accounting_actions, PanelEvaluationAccess access = {});
    ~StabilityExtensionStudy();
    [[nodiscard]] nlohmann::json plan() const;
    // One source session is loaded once; every variant sees its own prior state.
    [[nodiscard]] std::vector<nlohmann::json> append(std::span<const PanelReplayDay> primary,
                                                     std::span<const SessionDownload> sessions,
                                                     std::span<const std::optional<MinuteBar>> recovered,
                                                     bool terminal);
    [[nodiscard]] StabilityInputs observations() const;
    [[nodiscard]] nlohmann::json finish(const StabilityStudy::Progress &progress = {}) const;
    [[nodiscard]] static nlohmann::json numerical_checks();

  private:
    class State;
    std::unique_ptr<State> state_;
};
} // namespace trading
