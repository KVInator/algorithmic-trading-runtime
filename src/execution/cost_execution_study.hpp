#pragma once
#include "cost_execution_plan.hpp"
#include "execution_measurement.hpp"

namespace trading::execution {
class CostExecutionStudy {
  public:
    using Consumer = std::function<void(const nlohmann::json &)>;
    using Progress = std::function<void(std::size_t, std::size_t)>;
    [[nodiscard]] nlohmann::json plan() const;
    void run(const Consumer &consume, const Progress &progress = {}) const;
};
} // namespace trading::execution
