#pragma once

#include "development_study_dataset.hpp"
#include "long_cash_account.hpp"

#include <functional>
#include <span>

namespace trading {

struct RiskSessionCoverage {
    std::string date;
    std::size_t expected_minutes;
    std::size_t observed_minutes;
    bool account_references_complete;
    bool rv5_endpoints_complete;
    std::vector<std::string> missing_rv5_bar_starts;
    std::string evidence_sha256;
    std::string source;
};

// Coverage only: inspect raw endpoints without calculating returns or signals.
[[nodiscard]] std::vector<RiskSessionCoverage> audit_risk_session_coverage(
    std::span<const CalendarSession> calendar, std::span<const SessionDownload> downloads);

// Bounded capability probe, not a reconciled corporate-action ledger. The
// provider filters by process_date, which is not an ex-date coverage guarantee.
struct CorporateActionProbe {
    int year;
    std::string request_path;
    std::uint32_t http_status;
    std::string body;
    std::string body_sha256;
    bool json_object;
    bool more_pages;
    std::string retrieved_utc;
};
[[nodiscard]] CorporateActionProbe inspect_corporate_action_probe(int year, const MarketDataResponse& response);

struct RiskPreflightResult {
    std::string input_manifest_sha256;
    std::vector<std::pair<std::string, std::string>> calendar_hashes;
    std::vector<RiskSessionCoverage> coverage;
    AccountingDemonstration accounting;
    std::vector<std::string> blockers;
    std::string supplemental_manifest_sha256;
    bool protocol_registered{false};
    // Verified inputs are retained for the registered study adapter.
    std::vector<CalendarSession> calendar;
    std::vector<SessionDownload> sessions;
};

class RiskPreflight {
public:
    explicit RiskPreflight(std::filesystem::path project_root);
    [[nodiscard]] RiskPreflightResult run(const DevelopmentStudyDataset::Progress& progress = {}) const;
private:
    std::filesystem::path project_root_;
};

} // namespace trading
