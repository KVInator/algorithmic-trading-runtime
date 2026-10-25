#pragma once
#include "runtime_benchmark.hpp"

namespace trading {
class RuntimeBenchmarkReport {
  public:
    explicit RuntimeBenchmarkReport(const execution::RuntimeBenchmarkResult &result) : result_{result} {}
    void write(const std::filesystem::path &directory) const;

  private:
    const execution::RuntimeBenchmarkResult &result_;
};
} // namespace trading
