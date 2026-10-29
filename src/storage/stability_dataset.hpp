#pragma once
#include "stability_study.hpp"
#include <map>

namespace trading {
// Reads authenticated M6 exports only. It neither reacquires observations nor
// reopens the raw provider archive or a later-period strategy input.
class StabilityDataset {
  public:
    StabilityDataset(std::filesystem::path cost_directory, std::filesystem::path account_directory,
                     ExtensionStudyContract contract);
    // Calendar-aware conversion of one M6 cost/delay record. Shortened-session
    // paths are absent by design; eight unavailable observations retain the slot.
    [[nodiscard]] static std::map<std::string, StabilityObservation>
    intraday_observations(const nlohmann::json &record);
    [[nodiscard]] StabilityInputs load(const StabilityStudy::Progress &progress = {}) const;

  private:
    std::filesystem::path costs_, accounts_;
    ExtensionStudyContract contract_;
};
} // namespace trading
