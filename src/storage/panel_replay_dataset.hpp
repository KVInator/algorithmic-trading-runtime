#pragma once
#include "historical_panel.hpp"
#include "panel_replay.hpp"
#include <map>

namespace trading {
inline constexpr std::string_view registered_panel_study_sha256 =
    "edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c";

// Authenticates the frozen M2 registration once. Price access remains through
// HistoricalPanelDataset's scoped reader and raw-response verification.
// Default construction preserves the original pre-2024 boundary.
class PanelReplayDataset {
  public:
    PanelReplayDataset(std::filesystem::path directory, ExtensionStudyContract contract,
                       PanelInputPolicy policy, PanelSourceResolution resolution,
                       PanelOrigin origin = PanelOrigin::provider,
                       std::string expected_study_sha256 = std::string{registered_panel_study_sha256},
                       PanelEvaluationAccess access = {});
    [[nodiscard]] const nlohmann::json &provenance() const noexcept;
    [[nodiscard]] const nlohmann::json &accounting_actions() const noexcept;
    [[nodiscard]] std::optional<MinuteBar> recovered_close(const std::string &symbol,
                                                           const std::string &date) const;
    using Consumer = std::function<void(std::vector<PanelReplayDay>)>;
    void replay(const Consumer &consume, const HistoricalPanelDataset::Progress &progress = {}) const;
    using MarketConsumer =
        std::function<void(std::vector<PanelReplayDay>, std::vector<SessionDownload>)>;
    // Supply the same authenticated observations to execution research after
    // each causal decision record is complete, without loading prices twice.
    void replay_with_sessions(const MarketConsumer &consume,
                              const HistoricalPanelDataset::Progress &progress = {}) const;

  private:
    std::filesystem::path directory_;
    ExtensionStudyContract contract_;
    PanelInputPolicy policy_;
    PanelOrigin origin_;
    PanelEvaluationAccess access_;
    nlohmann::json manifest_, masks_, provenance_, actions_;
    std::map<std::string, double> dividends_;
    std::map<std::string, MinuteBar> recovered_;
    std::vector<CalendarRange> ranges_;
};
} // namespace trading
