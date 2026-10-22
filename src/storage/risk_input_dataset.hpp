#pragma once

#include "corporate_action_history.hpp"
#include "session_batch.hpp"

#include <functional>

namespace trading {

struct RiskPriceInput {
    std::string date;
    std::string role;
    HistoricalBarRequest request;
};

[[nodiscard]] std::vector<CalendarRange> risk_calibration_ranges();
[[nodiscard]] std::vector<RiskPriceInput> risk_price_input_plan();

struct RiskInputRecord {
    RiskPriceInput input;
    std::filesystem::path directory;
    std::string evidence_sha256;
    std::size_t observed_minutes;
};

struct RiskInputInventory {
    std::vector<RiskInputRecord> prices;
    CorporateActionHistory actions;
    std::vector<std::pair<std::string, std::string>> evidence_hashes;
    std::vector<std::string> blockers;
    std::string issuer_reference_sha256;
    bool cash_distributions_reconciled{false};
};

// One writer. Separate from all frozen development inputs. Existing evidence
// is verified and reused, including incomplete or rejected responses.
class RiskInputDataset {
public:
    using ActionFetcher = std::function<MarketDataResponse(const CorporateActionRequest&,
        const std::optional<std::string>&)>;
    explicit RiskInputDataset(std::filesystem::path directory, std::filesystem::path issuer_reference = {});
    [[nodiscard]] RiskInputInventory collect(const SessionBatch::CalendarFetcher& calendar_fetcher,
        const BarPageFetcher& bar_fetcher, const ActionFetcher& action_fetcher,
        const SessionBatch::Progress& progress = {}) const;
    [[nodiscard]] RiskInputInventory load() const;
    [[nodiscard]] std::vector<SessionDownload> supplementary_sessions() const;
private:
    [[nodiscard]] RiskInputInventory inspect(bool acquire, const SessionBatch::CalendarFetcher& calendar_fetcher,
        const BarPageFetcher& bar_fetcher, const ActionFetcher& action_fetcher,
        const SessionBatch::Progress& progress) const;
    std::filesystem::path directory_;
    std::filesystem::path issuer_reference_;
};

} // namespace trading
