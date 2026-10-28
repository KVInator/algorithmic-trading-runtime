#pragma once

#include "risk_input_dataset.hpp"
#include "long_cash_account.hpp"
#include "return_statistics.hpp"
#include "research_session.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace trading {

// Calendar-bounded raw references. Missing interiors remain missing; the last
// regular-session minute is mandatory in legacy mode, including for close-only
// calibration. Panel mode preserves an unavailable close explicitly.
class RiskSessionReferences {
public:
    RiskSessionReferences(CalendarSession calendar, std::vector<MinuteBar> bars,
        std::string symbol = "SPY", ResearchInputMode mode = ResearchInputMode::legacy_complete);
    [[nodiscard]] const CalendarSession& calendar() const noexcept;
    [[nodiscard]] std::optional<double> open_at(std::chrono::sys_seconds at) const;
    [[nodiscard]] std::optional<double> close_at(std::chrono::sys_seconds bar_start) const;
    [[nodiscard]] double close() const;
    [[nodiscard]] std::optional<double> observed_close() const;
private:
    CalendarSession calendar_;
    std::vector<MinuteBar> bars_;
};

struct RiskStudyInputs {
    double boundary_close;
    std::vector<RiskSessionReferences> calibration;
    std::vector<RiskSessionReferences> development;
    std::vector<CashDistribution> distributions;
    std::string development_manifest_sha256;
    std::string acquisition_manifest_sha256;
    std::string issuer_reference_sha256;
    std::string protocol_sha256;
    bool synthetic{false};
    std::vector<std::pair<std::string, std::string>> input_evidence;
};

class DailyRiskDataset {
public:
    explicit DailyRiskDataset(std::filesystem::path project_root);
    [[nodiscard]] RiskStudyInputs load(const SessionBatch::Progress& progress = {}) const;
private:
    std::filesystem::path root_;
};

struct DailyRiskMeasurement {
    std::string date;
    double log_return;
    double daily2;
    std::optional<double> rv1;
    std::optional<double> rv5;
    std::optional<double> rv15;
};

[[nodiscard]] DailyRiskMeasurement measure_daily_risk(const RiskSessionReferences& session,
    double previous_close, double ex_dividend);
struct LaggedRiskEstimate {
    double daily;
    std::optional<double> intraday;
    std::vector<std::string> missing_dates;
};
class LaggedRiskEstimator {
public:
    [[nodiscard]] LaggedRiskEstimate estimate(std::span<const DailyRiskMeasurement> prior) const;
};
struct HoldingRiskMeasurement {
    double endpoint2;
    std::optional<double> rv5;
};
[[nodiscard]] HoldingRiskMeasurement measure_holding_risk(const RiskSessionReferences& first,
    const RiskSessionReferences& next, double next_ex_dividend, int delay_minutes);

struct RiskForecast {
    std::string date;
    int delay_minutes;
    std::string cutoff_utc;
    std::string history_first;
    std::string history_last;
    double h_daily;
    std::optional<double> h_intraday;
    double used_h;
    double intraday_weight;
    double daily_weight;
    std::vector<std::string> missing_history_dates;
    std::optional<double> future_rv5;
    std::optional<double> future_endpoint2;
    std::optional<double> qlike_increment;
    std::optional<double> squared_error_increment;
    std::optional<double> endpoint_qlike_increment;
};

[[nodiscard]] double risk_budget_weight(double positive_second_moment);

struct RiskAccountDay {
    std::string date;
    double previous_equity_usd;
    double opening_equity_usd;
    AccountValue close;
    double net_return;
    double overnight_pnl_usd;
    double intraday_pnl_usd;
    double cost_usd;
    double traded_notional_usd;
    double target_weight;
    double closing_stock_weight;
    bool fallback;
    bool terminal_liquidation;
};

struct RiskAccountCase {
    std::string account;
    int delay_minutes;
    double cost_bps;
    std::vector<RiskAccountDay> days;
    std::vector<AccountEvent> ledger;
};

[[nodiscard]] double certainty_equivalent(std::span<const double> returns);
struct RiskContrast {
    double cer_over_daily;
    double cer_over_static;
    std::optional<double> qlike_increment;
    std::size_t forecast_records;
};
struct RiskBootstrap {
    std::size_t block_length;
    std::size_t replicates;
    std::uint64_t seed;
    PercentileInterval cer_over_daily;
    PercentileInterval cer_over_static;
    std::optional<PercentileInterval> qlike_increment;
    std::size_t undefined_loss_replicates;
};
struct RiskGate {
    std::string name;
    bool passed;
};
struct RiskCaseStatistics {
    std::string account;
    int delay_minutes;
    double cost_bps;
    std::string period;
    std::size_t sessions;
    ReturnStatistics returns;
    double cer;
    double traded_notional_usd;
    double turnover_equity_units;
    double cost_usd;
    double mean_closing_liquid_stock_weight;
    double worst_daily_return;
    double daily_return_5th_percentile;
    double mean_bottom_five_returns;
};
struct RiskOmission {
    std::string kind;
    std::string label;
    std::vector<std::string> removed_dates;
    RiskContrast contrast;
};
struct RiskLossStatistics {
    int delay_minutes;
    std::size_t eligible_records;
    std::optional<double> qlike_increment;
    std::optional<double> squared_error_increment;
    std::optional<double> endpoint_qlike_increment;
};
struct DailyRiskResult {
    bool synthetic;
    std::string protocol_sha256;
    std::string development_manifest_sha256;
    std::string acquisition_manifest_sha256;
    std::string issuer_reference_sha256;
    double static_weight;
    double calibration_daily2;
    std::vector<DailyRiskMeasurement> measurements;
    std::vector<RiskForecast> forecasts;
    std::vector<RiskAccountCase> accounts;
    RiskContrast primary;
    std::vector<RiskBootstrap> uncertainty;
    std::vector<RiskGate> gates;
    std::vector<RiskCaseStatistics> statistics;
    std::vector<RiskOmission> omissions;
    std::vector<RiskLossStatistics> forecast_statistics;
    std::vector<std::pair<std::string, std::string>> input_evidence;
};

class DailyRiskStudy {
public:
    // Historical runs require the registered 10,000 replicates. Smaller counts
    // are available only for explicitly synthetic contract fixtures.
    explicit DailyRiskStudy(std::size_t bootstrap_replicates = 10000);
    [[nodiscard]] DailyRiskResult run(const RiskStudyInputs& inputs,
        const SessionBatch::Progress& progress = {}) const;
private:
    std::size_t replicates_;
};

[[nodiscard]] const RiskAccountCase& risk_account_case(const DailyRiskResult& result,
    std::string_view account, int delay_minutes, double cost_bps);

} // namespace trading
