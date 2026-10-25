#include "reversion_validation.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace trading {
namespace {
void observe(ReversionCoverageStatistic& statistic, const std::optional<PercentileInterval>& range,
    const std::optional<double>& truth) {
    if (!range || !truth) { return; }
    ++statistic.available;
    if (range->lower <= *truth && *truth <= range->upper) { ++statistic.covered; }
}
void finish(ReversionCoverageStatistic& statistic) {
    if (statistic.available == 0) { return; }
    const double n = static_cast<double>(statistic.available);
    const double p = static_cast<double>(statistic.covered) / n;
    statistic.conditional_coverage = p;
    statistic.monte_carlo_standard_error = std::sqrt(p * (1 - p) / n);
}
} // namespace

ReversionSimulator::ReversionSimulator(ReversionSimulationCase specification, std::uint64_t seed)
    : specification_{std::move(specification)}, engine_{seed} {
    const auto& s = specification_;
    if (!std::isfinite(s.slope) || std::abs(s.slope) > 1.1 || s.sessions < 5 || s.sessions > 100 ||
        !std::isfinite(s.observation_noise_ratio) || s.observation_noise_ratio < 0 ||
        !std::isfinite(s.anchor_noise_ratio) || s.anchor_noise_ratio < 0 ||
        !std::isfinite(s.session_correlation) || std::abs(s.session_correlation) >= 1 ||
        (s.stationary_start && std::abs(s.slope) >= 1)) {
        throw std::invalid_argument("invalid synthetic reversion case");
    }
}

double ReversionSimulator::normal() {
    // Open-unit midpoint from the high 52 bits; fixed Box-Muller mapping, without
    // implementation-dependent std::normal_distribution or cached variates.
    const double u = (static_cast<double>(engine_() >> 12U) + 0.5) * 0x1p-52;
    const double v = (static_cast<double>(engine_() >> 12U) + 0.5) * 0x1p-52;
    return std::sqrt(-2 * std::log(u)) * std::cos(2 * std::numbers::pi * v);
}

std::vector<std::vector<double>> ReversionSimulator::next() {
    const auto& s = specification_;
    std::array<double, reversion_observations> shocks{};
    for (auto& value : shocks) { value = normal(); }
    const double new_scale = std::sqrt(1 - s.session_correlation * s.session_correlation);
    std::vector<std::vector<double>> sessions;
    sessions.reserve(s.sessions);
    for (std::size_t d = 0; d < s.sessions; ++d) {
        for (auto& value : shocks) { value = s.session_correlation * value + new_scale * normal(); }
        const double anchor = s.anchor_noise_ratio * simulation_innovation_sd * normal();
        double latent = s.stationary_start ? simulation_equilibrium +
            simulation_innovation_sd / std::sqrt(1 - s.slope * s.slope) * shocks[0] : 0;
        std::vector<double> sequence;
        sequence.reserve(reversion_observations);
        for (std::size_t k = 0; k < reversion_observations; ++k) {
            if (k != 0) {
                latent = simulation_equilibrium * (1 - s.slope) + s.slope * latent + simulation_innovation_sd * shocks[k];
            }
            sequence.push_back(latent + s.observation_noise_ratio * simulation_innovation_sd * normal() - anchor);
        }
        sessions.push_back(std::move(sequence));
    }
    return sessions;
}

std::optional<double> ReversionSimulator::population_observed_slope() const {
    const auto& s = specification_;
    if (!s.stationary_start) { return std::nullopt; }
    const double variance = 1 / (1 - s.slope * s.slope);
    const double anchor = s.anchor_noise_ratio * s.anchor_noise_ratio;
    return (s.slope * variance + anchor) /
        (variance + s.observation_noise_ratio * s.observation_noise_ratio + anchor);
}

ReversionValidationStudy::ReversionValidationStudy(ReversionValidationSettings settings) : settings_{settings} {
    if (settings.trials == 0 || settings.trials > 100000 || settings.coverage_trials == 0 ||
        settings.coverage_trials > settings.trials) {
        throw std::invalid_argument("invalid Monte Carlo trial counts");
    }
    static_cast<void>(ReversionUncertainty{{settings.bootstrap_replicates, 1, settings.seed}});
}

std::vector<ReversionSimulationCase> ReversionValidationStudy::prescribed_cases() {
    return {{"fast_stationary", .5, 20, 0, 0, 0, true},
        {"slow_stationary", .9, 20, 0, 0, 0, true},
        {"near_unit_stationary", .98, 20, 0, 0, 0, true},
        {"unit_root", 1, 20, 0, 0, 0, false},
        {"explosive", 1.02, 20, 0, 0, 0, false},
        {"alternating", -.5, 20, 0, 0, 0, true},
        {"short_history", .9, 5, 0, 0, 0, true},
        {"long_history", .9, 60, 0, 0, 0, true},
        {"opening_transient", .9, 20, 0, 0, 0, false},
        {"observation_noise", .9, 20, 1, 0, 0, true},
        {"noisy_unit_root", 1, 20, 1, 0, 0, false},
        {"anchor_noise", .9, 20, 0, 5, 0, true},
        {"dependent_sessions", .9, 20, 0, 0, .75, true}};
}

ReversionValidationResult ReversionValidationStudy::run(std::span<const ReversionSimulationCase> specifications,
    const std::function<void(const std::string&)>& progress) const {
    if (specifications.empty()) { throw std::invalid_argument("no synthetic cases"); }
    ReversionValidationResult result{settings_, {}};
    for (std::size_t c = 0; c < specifications.size(); ++c) {
        const auto& s = specifications[c];
        if (progress) { progress("Synthetic sampling and interval coverage | " + s.label); }
        const auto seed = settings_.seed + static_cast<std::uint64_t>(c) * 1000003U;
        ReversionSimulator simulator{s, seed};
        ReversionCaseValidation row{s, seed, simulator.population_observed_slope(), {}, {}, {}, {}, {}, {}};
        std::optional<double> theta, half_life, move;
        if (s.slope > 0 && s.slope < 1) {
            theta = simulation_equilibrium;
            const double kappa = -std::log(s.slope) / 5;
            half_life = std::log(2.0) / kappa;
            move = (simulation_equilibrium - simulation_current) * std::exp(-kappa) * (-std::expm1(-kappa * 30));
        }
        for (const std::size_t block : {1U, 5U}) {
            row.intervals.push_back({block, settings_.coverage_trials, 0, 0, {}, {}, {}, {}});
        }
        long double errors = 0, squared_errors = 0;
        std::size_t coefficients = 0;
        for (std::size_t trial = 0; trial < settings_.trials; ++trial) {
            const auto sessions = simulator.next();
            if (trial == 0) { row.reference_sessions = sessions; }
            ReversionValidationTrial output{estimator_.fit(sessions), {}};
            ++row.status_counts.at(static_cast<std::size_t>(output.fit.status));
            if (output.fit.slope) {
                const long double error = *output.fit.slope - s.slope;
                errors += error; squared_errors += error * error; ++coefficients;
            }
            if (trial < settings_.coverage_trials) {
                for (auto& interval : row.intervals) {
                    // Same seed for both block conventions; independent stream per trial.
                    const auto bootstrap_seed = seed + 500000003U + static_cast<std::uint64_t>(trial);
                    const auto uncertainty = ReversionUncertainty{{settings_.bootstrap_replicates,
                        interval.block_length, bootstrap_seed}}.run(sessions, simulation_current, 1, 30);
                    interval.inadmissible_refits += settings_.bootstrap_replicates - uncertainty.admissible_refits;
                    interval.coefficient_refits += uncertainty.coefficient_refits;
                    observe(interval.slope, uncertainty.slope, s.slope);
                    observe(interval.equilibrium, uncertainty.equilibrium, theta);
                    observe(interval.half_life_minutes, uncertainty.half_life_minutes, half_life);
                    observe(interval.expected_log_move, uncertainty.expected_log_move, move);
                    output.uncertainty.push_back(uncertainty);
                }
            }
            row.trials.push_back(std::move(output));
        }
        if (coefficients != 0) {
            row.slope_bias = static_cast<double>(errors / static_cast<long double>(coefficients));
            row.slope_rmse = std::sqrt(static_cast<double>(squared_errors / static_cast<long double>(coefficients)));
        }
        for (auto& interval : row.intervals) {
            finish(interval.slope); finish(interval.equilibrium);
            finish(interval.half_life_minutes); finish(interval.expected_log_move);
        }
        result.cases.push_back(std::move(row));
    }
    return result;
}

} // namespace trading
