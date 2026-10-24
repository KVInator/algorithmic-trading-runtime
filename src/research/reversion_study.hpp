#pragma once

#include "development_dataset.hpp"
#include "intraday_momentum.hpp"
#include "reversion_strategy.hpp"
#include "reversion_uncertainty.hpp"

#include <functional>
#include <map>

namespace trading {
inline constexpr std::string_view reversion_protocol_digest =
    "1fa24089060ccfc82963ee53216927fb0fa9e0f10f02948eae5b72b4032207b9";
inline constexpr std::string_view reversion_settings_digest =
    "64640f797613466b43b6c0c8686f317aa6a5bcc5425985e931816a6910e8ff86";
inline constexpr std::string_view reversion_manifest_digest =
    "70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e";
inline constexpr std::size_t reversion_case_count = 40;
inline constexpr std::size_t reversion_metric_count = 88;
using ReversionProgress = std::function<void(const std::string&)>;

struct ReversionCase {
    ReversionPolicy policy;
    int delay;
    double cost;
};

[[nodiscard]] const std::array<ReversionCase, 40>& reversion_cases();

struct ReversionCaseReturn {
    double gross;
    double net;
};

struct ReversionStudyDay {
    std::string date;
    ReversionDecision decision;
    std::array<double, 2> entry_prices;
    std::array<double, 2> exit_prices;
    std::array<ReversionCaseReturn, 40> accounts;
    double realized_log_move;
    // Drift loss minus candidate loss, on the identical forecast mask.
    std::optional<double> squared_loss_contrast;
    std::optional<double> absolute_loss_contrast;
    std::vector<ReversionUncertaintyResult> parameter_ranges;
};

struct ReversionObservation {
    std::string date;
    std::optional<OpeningAnchorResidual> measurement;
};

struct ReversionPeriod {
    PeriodStatistics returns;
    ReturnStatistics gross;
    ReturnConcentration concentration;
    std::size_t holding_minutes;
    double mean_daily_turnover;
    double fraction_of_session_in_market;
};

struct ReversionCaseStatistics {
    ReversionPeriod overall;
    std::map<std::string, ReversionPeriod> years;
};

struct ReversionMetric {
    std::string label;
    std::size_t available;
    std::optional<double> mean;
};

struct ReversionMetricRange {
    std::size_t undefined_replicates;
    std::optional<PercentileInterval> range;
};

struct ReversionOutcomeUncertainty {
    std::size_t block_length;
    std::size_t replicates;
    std::uint64_t seed;
    std::array<ReversionMetricRange, reversion_metric_count> metrics;
};

struct ReversionGate {
    std::string label;
    bool passed;
};

struct ReversionAccountPoint {
    double equity_before;
    double fractional_quantity;
    double equity_after;
    double drawdown;
};

struct ReversionStudyResult {
    std::vector<ReversionObservation> observations;
    std::vector<ReversionStudyDay> days;
    std::vector<ReversionCaseStatistics> statistics;
    std::array<std::vector<ReversionAccountPoint>, reversion_case_count> paths;
    std::array<ReversionMetric, reversion_metric_count> metrics;
    std::vector<ReversionOutcomeUncertainty> uncertainty;
    std::vector<ReversionGate> economic_gates;
    std::vector<ReversionGate> forecast_gates;
    std::string status;
};

struct ReversionStudySettings {
    std::size_t parameter_refits = 499;
    std::size_t outcome_resamples = 10000;
};

class ReversionStudy {
public:
    explicit ReversionStudy(ReversionStudySettings settings = {});
    // Constructed fixtures use this boundary. The authenticated application uses
    // run_frozen, which rejects reduced research budgets and changed identities.
    [[nodiscard]] ReversionStudyResult replay(std::span<const SessionDownload> sessions,
                                              const ReversionProgress& progress = {}) const;
    void analyze(ReversionStudyResult& result, const ReversionProgress& progress = {}) const;
    void assess(ReversionStudyResult& result) const;
    [[nodiscard]] ReversionStudyResult run_frozen(const FrozenDevelopmentDataset& dataset,
                                                  const std::filesystem::path& root,
                                                  const ReversionProgress& progress = {}) const;
    static void verify_registration(const std::filesystem::path& root);

private:
    ReversionStudySettings settings_;
    ReversionStrategy strategy_{};
};
} // namespace trading
