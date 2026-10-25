#pragma once

#include "batch_momentum.hpp"
#include "development_dataset.hpp"

#include <functional>

namespace trading {

struct YearStatistics {
    std::string year;
    PeriodStatistics statistics;
};

struct CaseStatistics {
    PeriodStatistics overall;
    std::vector<YearStatistics> calendar_years;
};

enum class DevelopmentDecision {
    insufficient_active_days,
    no_gross_edge,
    cost_dominated,
    delay_stress_failed,
    undefined_signal_contrast,
    nonpositive_signal_contrast,
    eligible_for_unchanged_validation
};

[[nodiscard]] DevelopmentDecision assess_development(const PeriodStatistics &primary,
                                                     const PeriodStatistics &delay_stress);
[[nodiscard]] std::string development_decision_name(DevelopmentDecision decision);

struct DevelopmentStudyResult {
    std::vector<MomentumCaseSummary> cases;
    std::vector<CaseStatistics> statistics;
    std::vector<BootstrapStatistics> primary_bootstrap;
    DevelopmentDecision decision;
};

class DevelopmentStudy {
  public:
    using Progress = std::function<void(const std::string &)>;
    // Requires the revision-2 dataset returned by DevelopmentStudyDataset.
    [[nodiscard]] DevelopmentStudyResult run(const FrozenDevelopmentDataset &dataset,
                                             const Progress &progress = {}) const;
};

} // namespace trading
