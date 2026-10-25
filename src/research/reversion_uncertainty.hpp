#pragma once

#include "intraday_reversion.hpp"
#include "return_statistics.hpp"

#include <array>
#include <cstdint>

namespace trading {

struct ReversionUncertaintySettings {
    std::size_t replicates = 499;
    std::size_t block_length = 1;
    std::uint64_t seed = 20261023;
};
struct ReversionUncertaintyResult {
    ReversionUncertaintySettings settings;
    // In name(ReversionFitStatus) declaration order. Every refit is counted.
    std::array<std::size_t, 7> status_counts{};
    std::size_t coefficient_refits = 0;
    std::size_t admissible_refits = 0;
    std::optional<PercentileInterval> slope;
    // Conditional on an admissible refit; not unconditional confidence intervals.
    std::optional<PercentileInterval> equilibrium;
    std::optional<PercentileInterval> half_life_minutes;
    std::optional<PercentileInterval> expected_log_move;
};

// Resamples whole sessions with the shared non-circular block sampler. Missing
// coefficients invalidate the slope range. OU ranges require 20 valid refits;
// their excluded probability mass remains explicit in status_counts.
class ReversionUncertainty {
public:
    explicit ReversionUncertainty(ReversionUncertaintySettings settings);
    [[nodiscard]] ReversionUncertaintyResult run(std::span<const std::vector<double>> sessions,
        std::optional<double> current, double delay_minutes, double holding_minutes) const;
private:
    ReversionUncertaintySettings settings_;
    PooledReversionEstimator estimator_{};
};

} // namespace trading
