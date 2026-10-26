#pragma once

#include "extension_contract.hpp"
#include "api_request_policy.hpp"
#include "session_download.hpp"
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <vector>

namespace trading {
enum class PanelOrigin { provider, synthetic };

struct PanelFetchers {
    std::function<MarketDataResponse(const CalendarRange&)> calendar;
    BarPageFetcher bars;
    std::function<MarketDataResponse(const CorporateActionRequest&, const std::optional<std::string>&)> actions;
};

// A separate immutable acquisition vintage. Every page is retained before
// decoding; interrupted requests and completed gapped sessions resume in place.
// The public price reader is restricted to pre-2024 development inputs.
class HistoricalPanelDataset {
public:
    using Progress = std::function<void(const std::string&)>;
    HistoricalPanelDataset(std::filesystem::path directory, ExtensionStudyContract contract,
        PanelOrigin origin, std::vector<CalendarRange> constructed_ranges = {});
    [[nodiscard]] nlohmann::json collect(const PanelFetchers& fetchers,
        ApiRequestPolicy::Wait wait, const Progress& progress = {}) const;
    [[nodiscard]] nlohmann::json audit(const Progress& progress = {}) const;
    [[nodiscard]] SessionDownload load_development_session(std::string_view symbol,
        std::string_view date, std::string_view expected_acquisition_sha256) const;
private:
    [[nodiscard]] nlohmann::json inspect(bool acquire, const PanelFetchers& fetchers,
        ApiRequestPolicy::Wait wait, const Progress& progress) const;
    std::filesystem::path directory_;
    ExtensionStudyContract contract_;
    PanelOrigin origin_;
    std::vector<CalendarRange> ranges_;
};
} // namespace trading
