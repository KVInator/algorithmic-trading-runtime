#pragma once

#include "continuation_study.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

enum class ScheduledRelease : unsigned { cpi = 1, employment = 2, fomc = 4 };

struct ScheduledReleaseRecord {
    std::string date;
    ScheduledRelease kind;
    std::string time_new_york;
    std::string source_url;
};

// Descriptive calendar labels only. No release values, surprises or trading
// decisions enter this boundary.
class ScheduledReleaseCalendar {
public:
    explicit ScheduledReleaseCalendar(std::string_view csv);
    [[nodiscard]] unsigned flags(std::string_view date) const;
    [[nodiscard]] const std::vector<ScheduledReleaseRecord>& records() const noexcept;
private:
    std::vector<ScheduledReleaseRecord> records_;
};

[[nodiscard]] std::string scheduled_release_label(unsigned flags);

enum class ExposureMatch { shared, delayed_entry, skipped };
[[nodiscard]] std::string_view exposure_match_name(ExposureMatch value);

struct RemovedExposure {
    std::string date;
    ExposureMatch match;
    ScheduledRoundTrip price_trade;
    std::optional<ScheduledRoundTrip> volume_trade;
    // Additive attribution in natural logarithms, not simple return units.
    double removed_gross_log_return;
    double saved_cost_log_drag;
    double net_log_increment;
};

struct ExposureAttribution {
    ExposureMatch match;
    std::size_t price_trades{};
    std::size_t removed_positive_segments{};
    std::size_t removed_negative_segments{};
    double gross_log_increment{};
    double saved_cost_log_drag{};
    double net_log_increment{};
};

struct PairedDiagnosticPeriod {
    std::string label;
    std::size_t sessions{};
    std::array<std::size_t, 2> active_days{};
    std::array<ReturnStatistics, 2> gross{};
    std::array<ReturnStatistics, 2> net{};
    double paired_net_mean{};
    double paired_gross_mean{};
    std::vector<std::string> cash_dates;
};

struct OpportunityDiagnostic {
    std::string date;
    std::size_t elapsed_minute;
    unsigned release_flags;
    bool price_condition;
    bool high_volume;
    double move;
    std::optional<double> normalized_move;
    double prefix_realized_volatility;
    double forward_gross_return;
};

struct ClockCovariates {
    std::size_t count{};
    std::size_t normalized_count{};
    double move_sum{};
    double normalized_move_sum{};
    double realized_volatility_sum{};
    double forward_gross_sum{};
};

struct EntryTimingDiagnostic {
    std::string date;
    ContinuationVariant variant;
    std::chrono::sys_seconds decision;
    std::chrono::sys_seconds reference;
    double entry_gap; // Reference open / last completed close - 1.
    double entry_minute_range; // Ex-post high-low / open; not a quoted spread.
    double entry_minute_volume; // Ex-post shares; not available at entry.
};

struct EntryTimingSummary {
    std::size_t count{};
    double mean_signed_gap{};
    double mean_absolute_gap{};
    double mean_entry_minute_range{};
    double mean_entry_minute_volume{};
};

struct ContinuationDiagnosticsResult {
    std::vector<RemovedExposure> exposure;
    std::array<ExposureAttribution, 3> attribution{{
        {ExposureMatch::shared}, {ExposureMatch::delayed_entry}, {ExposureMatch::skipped}}};
    double gross_log_increment{};
    double saved_cost_log_drag{};
    double net_log_increment{};
    double mean_daily_saved_cost_drag{};
    std::vector<PairedDiagnosticPeriod> release_groups;
    std::vector<PairedDiagnosticPeriod> common_winner_removal;
    std::vector<PairedDiagnosticPeriod> leave_one_month_out;
    std::vector<OpportunityDiagnostic> opportunities;
    // Per clock: high volume, then other volume, both among price=true.
    std::array<std::array<ClockCovariates, 2>, 12> covariates{};
    std::vector<EntryTimingDiagnostic> entries;
    std::array<EntryTimingSummary, 2> entry_summaries{};
};

// Post-result development diagnostics. This class cannot modify strategy
// parameters, registered screens or later-evaluation eligibility.
class ContinuationDiagnostics {
public:
    [[nodiscard]] ContinuationDiagnosticsResult analyze(
        const ContinuationStudyResult& study,
        std::span<const SessionDownload> eligible_sessions,
        const ScheduledReleaseCalendar& calendar) const;
};

} // namespace trading
