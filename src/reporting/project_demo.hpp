#pragma once
#include <filesystem>
#include <cstddef>

namespace trading {
struct ProjectDemoResult {
    std::filesystem::path directory;
    std::size_t checks;
    std::size_t cases;
};
class ProjectDemo {
  public:
    [[nodiscard]] ProjectDemoResult run(const std::filesystem::path &output_root) const;
};
} // namespace trading
