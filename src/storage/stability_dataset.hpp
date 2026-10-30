#pragma once
#include "stability_study.hpp"
#include <map>

namespace trading {
// Reads authenticated account/reference exports without reopening the raw archive.
// Later saved exports require explicit scope; default access remains pre-2024.
class StabilityDataset {
  public:
    StabilityDataset(std::filesystem::path cost_directory, std::filesystem::path account_directory,
                     ExtensionStudyContract contract, PanelEvaluationAccess access = {});
    // Calendar-aware conversion of one M6 cost/delay record. Shortened-session
    // paths are absent by design; eight unavailable observations retain the slot.
    [[nodiscard]] static std::map<std::string, StabilityObservation>
    intraday_observations(const nlohmann::json &record, PanelEvaluationAccess access = {});
    [[nodiscard]] StabilityInputs load(const StabilityStudy::Progress &progress = {}) const;

  private:
    std::filesystem::path costs_, accounts_;
    ExtensionStudyContract contract_;
    PanelEvaluationAccess access_;
};
} // namespace trading
