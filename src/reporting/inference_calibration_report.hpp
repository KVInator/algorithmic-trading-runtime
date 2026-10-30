#pragma once
#include "inference_calibration.hpp"
namespace trading {
class InferenceCalibrationReport {
  public:
    explicit InferenceCalibrationReport(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] std::filesystem::path run(bool check,
                                            const StabilityStudy::Progress &progress = {}) const;

  private:
    std::filesystem::path root_;
};
} // namespace trading
