#pragma once
#include "stability_study.hpp"
#include <utility>

namespace trading {
class StabilityReport {
  public:
    [[nodiscard]] static std::filesystem::path
    write(const std::filesystem::path &directory, const StabilityStudy &study,
          const StabilityInputs &inputs, nlohmann::json results, bool include_numerical_checks,
          PanelEvaluationAccess access = {});
};
class StabilityDevelopment {
  public:
    explicit StabilityDevelopment(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] std::filesystem::path run(bool constructed, std::filesystem::path cost_directory = {},
                                            std::filesystem::path account_directory = {},
                                            const StabilityStudy::Progress &progress = {}) const;

  private:
    std::filesystem::path root_;
};
} // namespace trading
