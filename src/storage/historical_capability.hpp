#pragma once
#include "api_request_policy.hpp"
#include "session_download.hpp"
#include <array>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

namespace trading {
enum class CapabilityOrigin { provider, synthetic };

struct HistoricalCapabilityPlan {
    int first_year{2016};
    int last_year{2025};
    inline static constexpr std::array<std::string_view, 5> symbols{"SPY", "QQQ", "IWM", "TLT", "GLD"};
};
struct HistoricalCapabilityFetchers {
    std::function<MarketDataResponse(const CalendarRange&)> calendar;
    BarPageFetcher bars;
    std::function<MarketDataResponse(const CorporateActionRequest&, const std::optional<std::string>&)> actions;
    std::function<MarketDataResponse(const HistoricalTickRequest&)> ticks;
};

// Schema/interval checks expose counts and availability, never price summaries.
class HistoricalCapabilityAudit {
public:
    [[nodiscard]] nlohmann::json ticks(const HistoricalTickRequest& request, std::string_view body) const;
    [[nodiscard]] nlohmann::json actions(const CorporateActionRequest& request, std::string_view body) const;
};

class HistoricalCapabilityProbe {
public:
    HistoricalCapabilityProbe(HistoricalCapabilityPlan plan, CapabilityOrigin origin, ApiRequestPolicy::Wait wait);
    // New run directory only. Raw later-period evidence is sealed from the review bundle.
    [[nodiscard]] nlohmann::json run(const std::filesystem::path& directory,
        const HistoricalCapabilityFetchers& fetchers,
        const std::function<void(const std::string&)>& progress = {}) const;
private:
    HistoricalCapabilityPlan plan_;
    CapabilityOrigin origin_;
    ApiRequestPolicy policy_;
};
} // namespace trading
