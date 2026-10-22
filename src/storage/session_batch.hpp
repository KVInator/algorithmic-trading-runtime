#pragma once

#include "market_calendar.hpp"
#include "session_download.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

enum class BatchPurpose { engineering_pilot, development };

struct FrozenSessionBatch {
    std::filesystem::path directory;
    std::string manifest_sha256;
    std::vector<SessionDownload> sessions;
    std::vector<std::string> excluded_dates;
};

struct UnresolvedSession {
    std::string date;
    std::filesystem::path directory;
    SessionAudit audit;
};

struct BatchCollection {
    std::size_t complete_sessions;
    std::vector<UnresolvedSession> unresolved;
};

// One writer per directory. Published snapshots, checkpoints and manifests are
// immutable. Constructors perform no filesystem or network operations.
class SessionBatch {
public:
    using CalendarFetcher = std::function<MarketDataResponse(const CalendarRange&)>;
    using Progress = std::function<void(const std::string&)>;

    SessionBatch(CalendarRange range, std::size_t expected_calendar_sessions,
                 std::filesystem::path directory, BatchPurpose purpose = BatchPurpose::engineering_pilot);
    [[nodiscard]] FrozenSessionBatch acquire(const CalendarFetcher& calendar_fetcher,
                                             const BarPageFetcher& page_fetcher,
                                             const Progress& progress = {}) const;
    [[nodiscard]] FrozenSessionBatch load() const;
    // Read-only checkpoint verification against this batch's saved calendar.
    [[nodiscard]] SessionDownload load_session(std::string_view date) const;
    // Retains verified incomplete runs without requesting them again and
    // continues to later dates. Only complete sessions receive checkpoints;
    // any unresolved gap prevents the monthly manifest from being published.
    [[nodiscard]] BatchCollection collect(const CalendarFetcher& calendar_fetcher,
                                           const BarPageFetcher& page_fetcher,
                                           const Progress& progress = {}) const;
    // Copies only verified, referenced artifacts; manifest is published last.
    // A partial import resumes only when its existing bytes match the source.
    [[nodiscard]] FrozenSessionBatch import_from(const std::filesystem::path& source) const;

private:
    enum class GapPolicy { stop_and_retry, retain_and_continue };
    [[nodiscard]] BatchCollection acquire_sessions(const CalendarFetcher& calendar_fetcher,
                                                   const BarPageFetcher& page_fetcher,
                                                   const Progress& progress,
                                                   GapPolicy policy) const;
    CalendarRange range_;
    std::size_t expected_calendar_sessions_;
    std::filesystem::path directory_;
    BatchPurpose purpose_;
};

} // namespace trading
