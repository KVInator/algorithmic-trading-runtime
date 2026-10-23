#pragma once

#include "reversion_uncertainty.hpp"

#include <functional>
#include <random>
#include <string>

namespace trading {

struct ReversionSimulationCase {
    std::string label;
    double slope;
    std::size_t sessions;
    double observation_noise_ratio;
    double anchor_noise_ratio;
    double session_correlation;
    bool stationary_start;
};
inline constexpr double simulation_equilibrium = 0.001;
inline constexpr double simulation_innovation_sd = 0.001;
inline constexpr double simulation_current = -0.004;

// Known latent AR(1), additive independent observation error, session-common
// anchor error and optional dependence across matching shocks in adjacent days.
class ReversionSimulator {
public:
    ReversionSimulator(ReversionSimulationCase specification, std::uint64_t seed);
    [[nodiscard]] std::vector<std::vector<double>> next();
    [[nodiscard]] std::optional<double> population_observed_slope() const;
private:
    [[nodiscard]] double normal();
    ReversionSimulationCase specification_;
    std::mt19937_64 engine_;
};

struct ReversionValidationSettings {
    std::size_t trials = 1000;
    std::size_t coverage_trials = 200;
    std::size_t bootstrap_replicates = 199;
    std::uint64_t seed = 20261023;
};
struct ReversionValidationTrial {
    PooledReversionFit fit;
    std::vector<ReversionUncertaintyResult> uncertainty;
};
struct ReversionCoverageStatistic {
    std::size_t available = 0;
    std::size_t covered = 0;
    std::optional<double> conditional_coverage;
    std::optional<double> monte_carlo_standard_error;
};
struct ReversionIntervalValidation {
    std::size_t block_length;
    std::size_t trials;
    std::size_t inadmissible_refits;
    std::size_t coefficient_refits;
    ReversionCoverageStatistic slope;
    ReversionCoverageStatistic equilibrium;
    ReversionCoverageStatistic half_life_minutes;
    ReversionCoverageStatistic expected_log_move;
};
struct ReversionCaseValidation {
    ReversionSimulationCase specification;
    std::uint64_t seed;
    std::optional<double> population_observed_slope;
    std::vector<std::vector<double>> reference_sessions; // First trial, retained for independent OLS verification.
    std::vector<ReversionValidationTrial> trials;
    std::array<std::size_t, 7> status_counts{};
    std::optional<double> slope_bias;
    std::optional<double> slope_rmse;
    std::vector<ReversionIntervalValidation> intervals;
};
struct ReversionValidationResult {
    ReversionValidationSettings settings;
    std::vector<ReversionCaseValidation> cases;
};

class ReversionValidationStudy {
public:
    explicit ReversionValidationStudy(ReversionValidationSettings settings = {});
    [[nodiscard]] static std::vector<ReversionSimulationCase> prescribed_cases();
    [[nodiscard]] ReversionValidationResult run(std::span<const ReversionSimulationCase> cases,
        const std::function<void(const std::string&)>& progress = {}) const;
private:
    ReversionValidationSettings settings_;
    PooledReversionEstimator estimator_{};
};

} // namespace trading
