#pragma once
#include "historical_panel.hpp"
#include "panel_replay.hpp"
#include <map>

namespace trading {
inline constexpr std::string_view registered_panel_study_sha256 = "edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c";

// Authenticates the frozen M2 registration once. Price access remains through
// HistoricalPanelDataset's pre-2024 reader and its raw response verification.
class PanelReplayDataset {
public:
    PanelReplayDataset(std::filesystem::path directory, ExtensionStudyContract contract,
        PanelInputPolicy policy, PanelSourceResolution resolution, PanelOrigin origin = PanelOrigin::provider,
        std::string expected_study_sha256 = std::string{registered_panel_study_sha256});
    [[nodiscard]] const nlohmann::json& provenance() const noexcept;
    using Consumer = std::function<void(std::vector<PanelReplayDay>)>;
    void replay(const Consumer& consume, const HistoricalPanelDataset::Progress& progress = {}) const;
private:
    std::filesystem::path directory_;
    ExtensionStudyContract contract_;
    PanelInputPolicy policy_;
    PanelOrigin origin_;
    nlohmann::json manifest_, masks_, provenance_;
    std::map<std::string, double> dividends_;
    std::map<std::string, MinuteBar> recovered_;
    std::vector<CalendarRange> ranges_;
};
} // namespace trading
