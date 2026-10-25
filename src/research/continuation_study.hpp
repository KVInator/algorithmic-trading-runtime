#pragma once

#include "development_dataset.hpp"
#include "intraday_continuation.hpp"
#include "return_statistics.hpp"

#include <array>
#include <functional>
#include <map>
#include <string>

namespace trading {

inline constexpr std::string_view continuation_development_manifest_sha256 =
    "70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e";
inline constexpr std::size_t continuation_case_count = 20;
using StudyProgress = std::function<void(const std::string&)>;

struct ContinuationCase {
    ContinuationVariant variant;
    std::chrono::minutes delay;
    double cost_bps_per_side;
};

[[nodiscard]] std::string_view continuation_variant_name(ContinuationVariant variant);
[[nodiscard]] const std::array<ContinuationCase, continuation_case_count>& continuation_cases();
[[nodiscard]] std::size_t continuation_case_index(
    ContinuationVariant variant, std::chrono::minutes delay, double cost_bps_per_side);

struct ContinuationOpportunity {
    IntradayContinuationFeatures features;
    // Same opportunity for both strategies; the two fixed delayed forward
    // windows end at the next decision, or at scheduled liquidation.
    std::array<ScheduledRoundTrip, 2> forward;
};

struct ContinuationCaseDay {
    double gross_return{};
    double net_return{};
    double control_gross_return{};
    double control_net_return{};
    double turnover{}; // Buy plus sell reference notional / start-of-day equity.
    std::chrono::minutes minutes_in_market{};
    std::vector<ScheduledRoundTrip> trades;
};

struct ContinuationDay {
    std::string date;
    std::array<ContinuationCaseDay, continuation_case_count> cases;
    std::array<ContinuationOpportunity, 12> opportunities;
    std::array<std::array<ContinuationAction, 13>, 2> actions;
};

struct ContinuationPeriodStatistics {
    std::size_t sessions;
    std::size_t active_days;
    std::size_t round_trips;
    double fraction_of_session_in_market; // Total held minutes / (390 * sessions).
    double mean_daily_turnover;
    ReturnStatistics gross;
    ReturnStatistics net;
    ReturnStatistics control_net;
    ReturnConcentration concentration;
};

struct ContinuationCaseStatistics {
    ContinuationPeriodStatistics overall;
    std::map<std::string, ContinuationPeriodStatistics> years;
};

struct ClockGroups {
    std::size_t price_true_count{};
    std::size_t price_false_count{};
    std::size_t high_volume_count{}; // Both volume groups condition on price=true.
    std::size_t other_volume_count{};
    double price_true_sum{};
    double price_false_sum{};
    double high_volume_sum{};
    double other_volume_sum{};
};

struct ContinuationContrasts {
    std::array<ClockGroups, 12> clocks;
    std::optional<double> price;
    std::optional<double> volume;
};

struct ContinuationBootstrapStatistics {
    std::size_t block_length;
    std::size_t replicates;
    std::uint64_t seed;
    PercentileInterval price_net_mean_95;
    PercentileInterval volume_net_mean_95;
    PercentileInterval control_net_mean_95;
    PercentileInterval paired_net_increment_95;
    PercentileInterval paired_gross_increment_95;
    std::optional<PercentileInterval> price_contrast_95;
    std::optional<PercentileInterval> volume_contrast_95;
    std::size_t undefined_price_replicates;
    std::size_t undefined_volume_replicates;
};

struct ContinuationGate {
    std::string name;
    bool passed;
};

struct ContinuationAssessment {
    std::string status;
    std::vector<ContinuationGate> point_gates;
    std::vector<ContinuationGate> uncertainty_gates;
    bool later_evaluation_allowed;
};

struct ContinuationStudyResult {
    std::vector<ContinuationDay> days;
    std::vector<ContinuationCaseStatistics> statistics;
    ContinuationContrasts contrasts;
    std::vector<ContinuationBootstrapStatistics> bootstrap;
    double paired_net_increment;
    double paired_gross_increment;
    std::array<ContinuationAssessment, 2> variants;
    ContinuationAssessment volume_increment;
};

// Applies the registered directional screens to already computed statistics.
// Incremental-volume evidence is assessed separately in the study result.
[[nodiscard]] ContinuationAssessment assess_continuation_variant(
    const ContinuationStudyResult& result, ContinuationVariant variant);

class ContinuationStudy {
public:
    // Reusable numerical boundary for independently constructed contract
    // fixtures. Production entry uses run_frozen, not this overload.
    [[nodiscard]] std::vector<ContinuationDay> replay(
        std::span<const SessionDownload> eligible_sessions, const StudyProgress& progress = {}) const;
    [[nodiscard]] ContinuationStudyResult analyze(
        std::vector<ContinuationDay> days, const StudyProgress& progress = {}) const;
    // Requires the read-only DevelopmentStudyDataset loader, authenticates the
    // fixed manifest and exclusion identities, and never accesses later years.
    [[nodiscard]] ContinuationStudyResult run_frozen(
        const FrozenDevelopmentDataset& dataset, const StudyProgress& progress = {}) const;
};

} // namespace trading
