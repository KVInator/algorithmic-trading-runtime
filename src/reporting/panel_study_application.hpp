#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace trading {
using PanelStudyProgress = std::function<void(const std::string &)>;

// Application services compose authenticated inputs, authoritative engines and
// result-bound reports. The Boolean selects only the declared analytic fixture.
class CostDelayDevelopment {
  public:
    explicit CostDelayDevelopment(std::filesystem::path project_root) : root_(std::move(project_root)) {}
    [[nodiscard]] std::filesystem::path run(bool constructed,
                                            const PanelStudyProgress &progress = {}) const;

  private:
    std::filesystem::path root_;
};

class PanelAccountDevelopment {
  public:
    explicit PanelAccountDevelopment(std::filesystem::path project_root)
        : root_(std::move(project_root)) {}
    [[nodiscard]] std::filesystem::path run(bool constructed,
                                            const PanelStudyProgress &progress = {}) const;

  private:
    std::filesystem::path root_;
};
} // namespace trading
