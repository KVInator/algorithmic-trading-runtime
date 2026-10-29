#pragma once
#include "panel_account_study.hpp"
#include <fstream>

namespace trading {
class PanelAccountReport {
  public:
    PanelAccountReport(const std::filesystem::path &root, nlohmann::json provenance,
                       std::filesystem::path dataset, const ExtensionStudyContract &contract,
                       nlohmann::json actions);
    [[nodiscard]] const std::filesystem::path &directory() const noexcept;
    void append(std::span<const PanelReplayDay> days, std::span<const SessionDownload> sessions,
                std::span<const std::optional<MinuteBar>> recovered, bool terminal);
    [[nodiscard]] std::filesystem::path finish();

  private:
    std::filesystem::path directory_;
    PanelAccountStudy study_;
    nlohmann::json summary_;
    std::map<std::string, std::ofstream> primary_;
    std::ofstream accounts_;
    std::size_t dates_{}, bytes_{};
    bool terminal_{};
};
} // namespace trading
