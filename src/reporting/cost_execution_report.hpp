#pragma once
#include "cost_execution_study.hpp"
#include <fstream>

namespace trading {
class CostExecutionReport {
  public:
    explicit CostExecutionReport(const std::filesystem::path &run_root);
    [[nodiscard]] const std::filesystem::path &directory() const noexcept;
    void append(const nlohmann::json &record);
    [[nodiscard]] std::filesystem::path finish();

  private:
    std::filesystem::path directory_;
    std::ofstream stream_;
    nlohmann::json plan_, rows_ = nlohmann::json::array();
    std::size_t bytes_{};
    bool finished_{};
};
} // namespace trading
