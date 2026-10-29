#pragma once
#include "stability_study.hpp"

namespace trading {
// Reads authenticated M6 exports only. It neither reacquires observations nor
// reopens the raw provider archive or a later-period strategy input.
class StabilityDataset {
  public:
    StabilityDataset(std::filesystem::path cost_directory, std::filesystem::path account_directory,
                     ExtensionStudyContract contract);
    [[nodiscard]] StabilityInputs load(const StabilityStudy::Progress &progress = {}) const;

  private:
    std::filesystem::path costs_, accounts_;
    ExtensionStudyContract contract_;
};
} // namespace trading
