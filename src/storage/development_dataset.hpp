#pragma once

#include "session_batch.hpp"

namespace trading {

inline constexpr std::string_view frozen_march_pilot_sha256 =
    "a8d9489284efe604ab8350f945e0e5393ae769c6bc3a0350450a2328759416e5";

struct SessionExclusion {
    std::string date;
    std::string reason;
};

struct FrozenDevelopmentDataset {
    std::filesystem::path directory;
    std::string manifest_sha256;
    std::string march_manifest_sha256;
    std::vector<SessionDownload> sessions;
    std::vector<SessionExclusion> exclusions;
    std::string manifest_file{"manifest.json"};
    std::string data_policy_sha256{};
    std::string protocol_sha256{};
    std::size_t protocol_revision{1};
};

struct DevelopmentMonth {
    std::string id;
    CalendarRange range;
};

[[nodiscard]] std::vector<DevelopmentMonth> development_months();
[[nodiscard]] std::vector<SessionExclusion> development_calendar_exclusions();

struct DevelopmentCollection {
    std::size_t complete_sessions;
    std::vector<UnresolvedSession> unresolved;
    std::filesystem::path audit_directory;
};

// Fixed SPY-IM-01 development interval; no validation/final-period access.
class DevelopmentDataset {
public:
    DevelopmentDataset(std::filesystem::path directory, std::filesystem::path frozen_march,
                       std::string expected_march_sha256 = std::string{frozen_march_pilot_sha256});
    [[nodiscard]] FrozenDevelopmentDataset acquire(const SessionBatch::CalendarFetcher& calendar_fetcher,
                                                   const BarPageFetcher& page_fetcher,
                                                   const SessionBatch::Progress& progress = {}) const;
    [[nodiscard]] FrozenDevelopmentDataset load() const;
    // Completes acquisition attempts across the fixed period, retaining
    // unresolved coverage gaps. The audit is not a performance input.
    [[nodiscard]] DevelopmentCollection collect(const SessionBatch::CalendarFetcher& calendar_fetcher,
                                                const BarPageFetcher& page_fetcher,
                                                const std::filesystem::path& report_root,
                                                const SessionBatch::Progress& progress = {}) const;
private:
    [[nodiscard]] DevelopmentCollection acquire_months(const SessionBatch::CalendarFetcher& calendar_fetcher,
                                                       const BarPageFetcher& page_fetcher,
                                                       const SessionBatch::Progress& progress,
                                                       bool retain_gaps) const;
    std::filesystem::path directory_;
    std::filesystem::path frozen_march_;
    std::string expected_march_sha256_;
};

} // namespace trading
