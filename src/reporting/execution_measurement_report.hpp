#pragma once
#include "execution_measurement.hpp"

namespace trading {
class ExecutionMeasurementReport {
  public:
    explicit ExecutionMeasurementReport(const execution::ExecutionMeasurementResult &result)
        : result_{result} {}
    void write(const std::filesystem::path &directory) const;

  private:
    const execution::ExecutionMeasurementResult &result_;
};
} // namespace trading
