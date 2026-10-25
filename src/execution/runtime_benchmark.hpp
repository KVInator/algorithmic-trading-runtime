#pragma once
#include "paper_execution.hpp"
#include <functional>

namespace trading::execution {
struct ProcessResources {
    std::optional<std::uint64_t> resident_bytes;
    std::optional<std::uint64_t> peak_resident_bytes;
};
[[nodiscard]] ProcessResources process_resources();
[[nodiscard]] nlohmann::json benchmark_environment();
struct RuntimeBenchmarkConfiguration {
    std::vector<std::size_t> round_trips{8, 32, 128};
    std::size_t repetitions{5};
};
class LatencyDistribution {
  public:
    [[nodiscard]] nlohmann::json summarize(const std::vector<std::int64_t> &nanoseconds) const;
};
struct RuntimeBenchmarkResult {
    nlohmann::json environment;
    nlohmann::json rows = nlohmann::json::array();
    nlohmann::json workloads = nlohmann::json::array();
    nlohmann::json clock_probe;
};
class RuntimeBenchmark {
  public:
    using Progress = std::function<void(std::size_t completed, std::size_t total)>;
    [[nodiscard]] RuntimeBenchmarkResult run(const std::filesystem::path &directory,
                                             const RuntimeBenchmarkConfiguration &configuration = {},
                                             const Progress &progress = {}) const;
};
} // namespace trading::execution
