#pragma once
#include "inference_calibration.hpp"
namespace trading {
// Check capabilities are origin-bound; provider stages require verified receipts.
class EvaluationAuthorization {
  public:
    explicit EvaluationAuthorization(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] PanelEvaluationAccess validation(const std::filesystem::path &calibration,
                                                   bool constructed = false) const;
    [[nodiscard]] PanelEvaluationAccess final(const std::filesystem::path &freeze,
                                              const std::filesystem::path &runner) const;
    [[nodiscard]] std::filesystem::path freeze(const std::filesystem::path &validation,
                                               const std::filesystem::path &runner) const;
    [[nodiscard]] nlohmann::json checks(const PanelEvaluationAccess &access) const;

  private:
    std::filesystem::path root_;
};
} // namespace trading
