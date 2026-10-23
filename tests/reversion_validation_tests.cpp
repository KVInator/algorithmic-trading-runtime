#include "reversion_validation_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
using namespace trading;
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
bool near(double a, double b) { return std::abs(a - b) <= 1e-12 * std::max({1.0, std::abs(a), std::abs(b)}); }
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid synthetic or uncertainty input must fail");
}
void registration_contracts() {
    const std::filesystem::path root{TRADING_PROJECT_ROOT};
    require(sha256_file(root / "research/intraday_reversion_protocol.md") ==
        "1fa24089060ccfc82963ee53216927fb0fa9e0f10f02948eae5b72b4032207b9" &&
        sha256_file(root / "research/intraday_reversion_protocol.json") ==
        "64640f797613466b43b6c0c8686f317aa6a5bcc5425985e931816a6910e8ff86", "registered revision 1 identities remain fixed before historical scoring");
    const auto settings = nlohmann::json::parse(read_text_file(root / "research/intraday_reversion_protocol.json"));
    require(settings.at("revision").get<int>() == 1 && settings.at("scored_sessions").get<int>() == 476 &&
        settings.at("parameter_bootstrap").at("replicates").get<std::size_t>() == ReversionUncertaintySettings{}.replicates &&
        !settings.at("parameter_bootstrap").at("entry_filter").get<bool>(),
        "registered sample and descriptive-range settings match the uncertainty engine");
}
void uncertainty_contracts() {
    const std::vector<std::vector<double>> negative{{-2, 1.2}, {-1, .4}, {0, .3}, {1, -.4}, {2, -1.1}};
    const auto result = ReversionUncertainty{{40, 5, 42}}.run(negative, -.004, 1, 30);
    require(result.status_counts[static_cast<std::size_t>(ReversionFitStatus::nonpositive_slope)] == 40 &&
        result.coefficient_refits == 40 && result.admissible_refits == 0 && result.slope && !result.equilibrium &&
        !result.half_life_minutes && !result.expected_log_move, "unsupported OU refits retain raw slopes and all failure mass");
    // Independent ordinary least-squares sums, with no estimator invocation.
    double x = 0, y = 0, xx = 0, xy = 0;
    for (const auto& pair : negative) { x += pair[0]; y += pair[1]; xx += pair[0]*pair[0]; xy += pair[0]*pair[1]; }
    const double n = static_cast<double>(negative.size());
    const double slope = (xy-x*y/n)/(xx-x*x/n);
    require(near(result.slope->lower, slope) && near(result.slope->upper, slope), "full-history blocks produce the independent OLS point");
    const std::vector<std::vector<double>> flat(5, std::vector<double>{2, 2, 2});
    const auto singular = ReversionUncertainty{{40, 1, 42}}.run(flat, 0, 1, 30);
    require(singular.coefficient_refits == 0 && !singular.slope && singular.status_counts[2] == 40,
        "singular replicates make coefficient ranges unavailable");
    // Some resamples contain only a constant session. A slope range cannot be
    // reconstructed by silently discarding those draws.
    auto mixed = flat; mixed[0] = {-2, 0, 1};
    const auto partial = ReversionUncertainty{{100, 1, 7}}.run(mixed, 0, 1, 30);
    require(partial.coefficient_refits > 0 && partial.coefficient_refits < 100 && !partial.slope,
        "a mixture of coefficient failures invalidates the unconditional slope range");
    rejects([] { static_cast<void>(ReversionUncertainty{{19, 1, 1}}); });
    rejects([&] { static_cast<void>(ReversionUncertainty{{40, 6, 1}}.run(negative, 0, 1, 30)); });
    rejects([&] { static_cast<void>(ReversionUncertainty{{40, 1, 1}}.run(negative, 0, -1, 30)); });
    ReversionSimulator simulator{ReversionValidationStudy::prescribed_cases()[1], 123};
    const auto missing_current = ReversionUncertainty{{40, 1, 42}}.run(simulator.next(), std::nullopt, 1, 30);
    require(missing_current.equilibrium && missing_current.half_life_minutes && !missing_current.expected_log_move,
        "missing current state preserves history parameter ranges without a fabricated move");
    auto corrupt = negative; corrupt[4][0] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { static_cast<void>(ReversionUncertainty{{40, 5, 1}}.run(corrupt, 0, 1, 30)); });
}
ReversionValidationResult simulation_contracts() {
    const auto cases = ReversionValidationStudy::prescribed_cases();
    require(cases.size() == 13 && cases[3].slope == 1 && cases[10].observation_noise_ratio == 1,
        "prescribed suite includes unobserved and noisy non-reversion controls");
    ReversionSimulator a{cases[1], 123}, b{cases[1], 123};
    const auto sample = a.next();
    require(sample == b.next() && sample.size() == 20 && sample.front().size() == 25 && a.next() != sample,
        "same seed replays exactly and advancing the stream changes the draw");
    require(PooledReversionEstimator{}.fit(sample).pairs == 480, "no bootstrap or simulator cross-session pairs");
    const auto observed = ReversionSimulator{cases[9], 1}.population_observed_slope();
    require(observed && near(*observed, .9 / 1.19), "observation noise population attenuation has an independent covariance identity");
    require(!ReversionSimulator{cases[3], 1}.population_observed_slope(), "unit-root population covariance is not stationary");
    auto invalid = cases[1]; invalid.slope = 1;
    rejects([&] { static_cast<void>(ReversionSimulator{invalid, 1}); });
    rejects([] { static_cast<void>(ReversionValidationStudy{{1, 2, 40, 1}}); });
    // Small routine workload. Statistical conclusions come from the full app,
    // rather than a fragile assertion about a Monte Carlo success percentage.
    const std::vector<ReversionSimulationCase> selected{cases[1], cases[3], cases[6], cases[9]};
    const ReversionValidationStudy study{{8, 3, 40, 123}};
    auto result = study.run(selected);
    const auto repeat = study.run(selected);
    for (std::size_t i = 0; i < result.cases.size(); ++i) {
        const auto& c = result.cases[i];
        require(c.status_counts == repeat.cases[i].status_counts && c.slope_bias == repeat.cases[i].slope_bias,
            "Monte Carlo result replay is identical");
        require(std::accumulate(c.status_counts.begin(), c.status_counts.end(), std::size_t{0}) == 8,
            "all point fits retain a status");
        for (const auto& trial : c.trials) {
            for (const auto& u : trial.uncertainty) {
                require(std::accumulate(u.status_counts.begin(), u.status_counts.end(), std::size_t{0}) == 40,
                    "every bootstrap replicate is accounted for");
            }
        }
        for (const auto& v : c.intervals) {
            require(v.slope.available <= v.trials && v.slope.covered <= v.slope.available,
                "coverage keeps its true available denominator");
            if (c.specification.slope == 1) {
                require(v.half_life_minutes.available == 0 && !v.half_life_minutes.conditional_coverage,
                    "unit-root latent half-life is undefined even for apparently admissible estimates");
            }
        }
    }
    require(result.cases[2].intervals[1].slope.available == 3 && result.cases[2].intervals[1].slope.covered == 0,
        "five-session full-block failure control has degenerate ranges");
    return result;
}
void report_contracts(const std::filesystem::path& root, const ReversionValidationResult& result) {
    const auto dir = create_unique_directory(root, "reversion-validation");
    ReversionValidationReport{result}.write(dir);
    const auto summary = nlohmann::json::parse(read_text_file(dir / "summary.json"));
    require(summary.at("study_status").get<std::string>() == "synthetic_only" &&
        summary.at("historical_model_fits").get<int>() == 0 && summary.at("historical_strategy_scores").get<int>() == 0,
        "simulation cannot imply historical fit or scoring");
    require(summary.at("cases")[0].at("slope_bias").is_number() &&
        summary.at("cases")[1].at("population_observed_slope").is_null(), "optional report values are scalars or null");
    for (const auto& artifact : summary.at("artifacts")) {
        const auto file = artifact.at("path").get<std::string>();
        require(sha256_file(dir / file) == artifact.at("sha256").get<std::string>(), "saved artifact identity matches manifest");
    }
    const auto trials = nlohmann::json::parse(read_text_file(dir / "trials_0.json"));
    require(trials.size() == 8 && trials[0].at("uncertainty").size() == 2 && trials[7].at("uncertainty").empty(),
        "all point trials and the declared coverage subset are exported");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("validation contracts require an output root"); }
        registration_contracts();
        uncertainty_contracts();
        const auto result = simulation_contracts();
        report_contracts(argv[1], result);
        std::cout << "Reversion uncertainty, simulation and report contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Reversion validation contract failure: " << error.what() << '\n';
        return 1;
    }
}
