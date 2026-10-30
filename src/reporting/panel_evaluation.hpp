#pragma once
#include "evaluation_authorization.hpp"
#include "stability_study.hpp"
namespace trading {
class PanelEvaluation {
  public:
    explicit PanelEvaluation(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] std::filesystem::path run(const PanelEvaluationAccess &access,
                                            const std::filesystem::path &calibration,
                                            const std::filesystem::path &runner,
                                            const StabilityStudy::Progress &progress = {},
                                            std::filesystem::path final_freeze = {}) const;

  private:
    std::filesystem::path root_;
};
} // namespace trading
