#pragma once
#include "stability_extension.hpp"
#include <fstream>

namespace trading {
class StabilityExtensionReport {
  public:
    StabilityExtensionReport(std::filesystem::path run_root, nlohmann::json provenance,
                             std::filesystem::path dataset, const ExtensionStudyContract &contract,
                             PanelInputPolicy policy, nlohmann::json actions,
                             PanelEvaluationAccess access = {});
    [[nodiscard]] const std::filesystem::path &directory() const noexcept;
    void append(std::span<const PanelReplayDay> primary, std::span<const SessionDownload> sessions,
                std::span<const std::optional<MinuteBar>> recovered, bool terminal);
    [[nodiscard]] std::filesystem::path finish(const StabilityStudy::Progress &progress = {});

  private:
    std::filesystem::path directory_;
    StabilityExtensionStudy study_;
    nlohmann::json summary_;
    std::map<std::string, std::ofstream> streams_;
};
class StabilityExtensionDevelopment {
  public:
    explicit StabilityExtensionDevelopment(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] std::filesystem::path run(bool constructed,
                                            const StabilityStudy::Progress &progress = {}) const;

  private:
    std::filesystem::path root_;
};
} // namespace trading
