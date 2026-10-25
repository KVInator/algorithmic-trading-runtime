#pragma once
#include "development_study.hpp"
#include <filesystem>

namespace trading {
class MomentumStudyReport {
  public:
    MomentumStudyReport(const FrozenDevelopmentDataset &dataset, const DevelopmentStudyResult &study)
        : dataset_{dataset}, study_{study} {}
    [[nodiscard]] std::filesystem::path write(const std::filesystem::path &output_root) const;

  private:
    const FrozenDevelopmentDataset &dataset_;
    const DevelopmentStudyResult &study_;
};
} // namespace trading
