#include "reversion_uncertainty.hpp"

#include <cmath>
#include <stdexcept>

namespace trading {

ReversionUncertainty::ReversionUncertainty(ReversionUncertaintySettings settings) : settings_{settings} {
    if (settings.replicates < 20 || settings.replicates > 100000 || settings.block_length == 0) {
        throw std::invalid_argument("reversion uncertainty requires 20..100000 refits and a positive session block");
    }
}

ReversionUncertaintyResult ReversionUncertainty::run(std::span<const std::vector<double>> sessions,
    std::optional<double> current, double delay_minutes, double holding_minutes) const {
    if ((current && !std::isfinite(*current)) || !std::isfinite(delay_minutes) || delay_minutes < 0 ||
        !std::isfinite(holding_minutes) || holding_minutes <= 0) {
        throw std::invalid_argument("invalid reversion uncertainty state or horizon");
    }
    // Validate all source observations, including sessions a resample might omit.
    static_cast<void>(estimator_.fit(sessions));
    MovingBlockIndexSampler sampler{sessions.size(), settings_.block_length, settings_.seed};
    ReversionUncertaintyResult result{};
    result.settings = settings_;
    std::vector<double> slopes, equilibria, half_lives, moves;
    std::vector<std::vector<double>> sampled;
    sampled.reserve(sessions.size());
    for (std::size_t b = 0; b < settings_.replicates; ++b) {
        sampled.clear();
        for (const auto index : sampler.next()) { sampled.push_back(sessions[index]); }
        const auto fit = estimator_.fit(sampled);
        ++result.status_counts.at(static_cast<std::size_t>(fit.status));
        if (fit.slope) { slopes.push_back(*fit.slope); }
        if (fit.status != ReversionFitStatus::admissible) { continue; }
        equilibria.push_back(*fit.equilibrium);
        half_lives.push_back(*fit.half_life_minutes);
        if (current) { moves.push_back(estimator_.expected_log_move(fit, *current, delay_minutes, holding_minutes)); }
    }
    result.coefficient_refits = slopes.size();
    result.admissible_refits = equilibria.size();
    if (slopes.size() == settings_.replicates) { result.slope = percentile_interval(std::move(slopes)); }
    if (equilibria.size() >= 20) {
        result.equilibrium = percentile_interval(std::move(equilibria));
        result.half_life_minutes = percentile_interval(std::move(half_lives));
        if (current) { result.expected_log_move = percentile_interval(std::move(moves)); }
    }
    return result;
}

} // namespace trading
