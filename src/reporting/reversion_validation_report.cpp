#include "reversion_validation_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;
Json number(const std::optional<double>& v) { return v ? Json(*v) : Json(nullptr); }
Json range(const std::optional<PercentileInterval>& v) {
    return v ? Json{{"lower", v->lower}, {"upper", v->upper}} : Json(nullptr);
}
Json coverage(const ReversionCoverageStatistic& s) {
    return {{"available", s.available}, {"covered", s.covered},
        {"coverage_given_available", number(s.conditional_coverage)},
        {"monte_carlo_standard_error", number(s.monte_carlo_standard_error)}};
}
Json uncertainty(const ReversionUncertaintyResult& u) {
    return {{"block_length", u.settings.block_length}, {"replicates", u.settings.replicates}, {"seed", u.settings.seed},
        {"status_counts", u.status_counts}, {"coefficient_refits", u.coefficient_refits}, {"admissible_refits", u.admissible_refits},
        {"slope", range(u.slope)}, {"conditional_equilibrium", range(u.equilibrium)},
        {"conditional_half_life_minutes", range(u.half_life_minutes)}, {"conditional_expected_log_move", range(u.expected_log_move)}};
}
} // namespace

void ReversionValidationReport::write(const std::filesystem::path& directory) const {
    Json cases = Json::array(), artifacts = Json::array(), statuses = Json::array();
    std::string trial_links;
    for (std::size_t i = 0; i < 7; ++i) { statuses.push_back(name(static_cast<ReversionFitStatus>(i))); }
    std::ostringstream html, samples;
    samples << std::setprecision(17) << "case,session,observation,residual\n";
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Reversion sampling and noise diagnostic</title><style>body{font:16px system-ui;max-width:1250px;margin:2rem auto;"
        "padding:0 1rem;color:#192c40;background:#f6f8fb}table{border-collapse:collapse;background:white;width:100%}th,td{padding:9px;"
        "border-bottom:1px solid #ccd7e2;text-align:right}th:first-child,td:first-child{text-align:left}.scroll{overflow:auto}"
        "code{overflow-wrap:anywhere}</style><main><h1>Reversion sampling and observation noise</h1>"
        "<p>Synthetic-only diagnostic. No historical prices are loaded, no historical model is fitted and no strategy is scored.</p>"
        "<p>Latent innovation SD = 0.001, equilibrium = 0.001. Each session contains 25 observations. "
        "Bias and RMSE refer to the latent slope. Observation error and session-common anchor error can change the population "
        "coefficient of the observed regression; sample size cannot remove that misspecification.</p>"
        "<h2>Point estimates</h2><div class=\"scroll\"><table><tr><th>Case</th><th>Sessions</th><th>Latent slope</th>"
        "<th>Population observed slope</th><th>Bias</th><th>RMSE</th><th>OU admissible / trials</th></tr>";
    html << std::setprecision(6);
    for (const auto& c : result_.cases) {
        Json trials = Json::array();
        const auto trial_file = "trials_" + std::to_string(cases.size()) + ".json";
        const auto& s = c.specification;
        for (std::size_t d = 0; d < c.reference_sessions.size(); ++d) {
            for (std::size_t k = 0; k < c.reference_sessions[d].size(); ++k) {
                samples << ReportText{s.label}.csv() << ',' << d << ',' << k << ',' << c.reference_sessions[d][k] << '\n';
            }
        }
        Json intervals = Json::array();
        for (const auto& v : c.intervals) {
            intervals.push_back({{"block_length", v.block_length}, {"trials", v.trials},
                {"inadmissible_refits", v.inadmissible_refits}, {"coefficient_refits", v.coefficient_refits},
                {"slope", coverage(v.slope)}, {"equilibrium", coverage(v.equilibrium)},
                {"half_life_minutes", coverage(v.half_life_minutes)}, {"expected_log_move", coverage(v.expected_log_move)}});
        }
        cases.push_back({{"label", s.label}, {"trials_file", trial_file}, {"latent_slope", s.slope}, {"sessions", s.sessions},
            {"observation_noise_to_innovation_sd", s.observation_noise_ratio}, {"anchor_noise_to_innovation_sd", s.anchor_noise_ratio},
            {"session_correlation", s.session_correlation}, {"stationary_start", s.stationary_start}, {"seed", c.seed},
            {"population_observed_slope", number(c.population_observed_slope)}, {"status_counts", c.status_counts},
            {"slope_bias", number(c.slope_bias)}, {"slope_rmse", number(c.slope_rmse)}, {"interval_validation", intervals}});
        html << "<tr><td>" << ReportText{s.label}.html() << "</td><td>" << s.sessions << "</td><td>" << s.slope << "</td><td>"
            << (c.population_observed_slope ? std::to_string(*c.population_observed_slope) : "undefined")
            << "</td><td>" << (c.slope_bias ? std::to_string(*c.slope_bias) : "undefined") << "</td><td>"
            << (c.slope_rmse ? std::to_string(*c.slope_rmse) : "undefined") << "</td><td>" << c.status_counts[0] << " / "
            << c.trials.size() << "</td></tr>";
        for (std::size_t t = 0; t < c.trials.size(); ++t) {
            const auto& trial = c.trials[t];
            const auto& f = trial.fit;
            Json ranges = Json::array();
            for (const auto& u : trial.uncertainty) { ranges.push_back(uncertainty(u)); }
            trials.push_back({{"case", s.label}, {"trial", t}, {"status", name(f.status)}, {"pairs", f.pairs},
                {"intercept", number(f.intercept)}, {"slope", number(f.slope)}, {"innovation_variance", number(f.innovation_variance)},
                {"equilibrium", number(f.equilibrium)}, {"half_life_minutes", number(f.half_life_minutes)}, {"uncertainty", ranges}});
        }
        write_new_text_file(directory / trial_file, trials.dump(2) + '\n');
        artifacts.push_back({{"path", trial_file}, {"sha256", sha256_file(directory / trial_file)}});
        trial_links += "<li><a href=\"" + trial_file + "\">" + ReportText{s.label}.html() + " trial records</a></li>";
    }
    html << "</table></div><p>Positive sub-unit OLS slopes are numerical OU admissibility conditions, not stationarity tests. "
        "The unit-root controls measure false admissibility directly.</p><h2>Nominal 95% percentile ranges: empirical coverage</h2>"
        "<p>Whole ordered sessions are sampled in non-circular blocks of 1 and 5. Slope ranges require coefficients in every refit. "
        "OU-derived ranges condition on admissible refits and require at least 20. Coverage denominators below count computable "
        "ranges with a defined latent truth, with Monte Carlo standard errors. Zero availability is undefined. "
        "Covered / all coverage trials is also retained through the trial count in the manifest; missing ranges are never replaced. "
        "These diagnostics do not certify nominal coverage or provide an entry filter.</p><div class=\"scroll\"><table>"
        "<tr><th>Case / block</th><th>Slope covered / available</th><th>Equilibrium covered / available</th>"
        "<th>Half-life covered / available</th><th>Move covered / available</th><th>Invalid OU refits / all</th></tr>";
    for (const auto& c : result_.cases) {
        for (const auto& v : c.intervals) {
            html << "<tr><td>" << ReportText{c.specification.label}.html() << " / " << v.block_length << "</td>";
            for (const auto* statistic : {&v.slope, &v.equilibrium, &v.half_life_minutes, &v.expected_log_move}) {
                html << "<td>" << statistic->covered << " / " << statistic->available;
                if (statistic->monte_carlo_standard_error) { html << " (SE " << *statistic->monte_carlo_standard_error << ')'; }
                html << "</td>";
            }
            html << "<td>" << v.inadmissible_refits << " / " << v.trials * result_.settings.bootstrap_replicates << "</td></tr>";
        }
    }
    html << "</table></div><p>With five source sessions and block length five the resample is the original sample on every draw: "
        "degenerate ranges are an expected failure control. Long-history and dependence cases retain the same estimator. "
        "The anchor error is a constructed session-common perturbation, not a calibrated model of the price-derived opening anchor.</p>"
        "<ul>" << trial_links << "</ul><p><a href=\"summary.json\">Settings, counts and coverage</a> | "
        "<a href=\"reference_sessions.csv\">First-trial observations</a> | "
        "<a href=\"source_inventory.txt\">Source inventory</a></p><p>Source SHA-256: <code>" << build::source_sha256 << "</code></p></main></html>\n";
    write_new_text_file(directory / "reference_sessions.csv", samples.str());
    write_new_text_file(directory / "source_inventory.txt", build::source_records);
    write_new_text_file(directory / "report.html", html.str());
    for (const auto* file : {"reference_sessions.csv", "source_inventory.txt", "report.html"}) {
        artifacts.push_back({{"path", file}, {"sha256", sha256_file(directory / file)}});
    }
    const auto& settings = result_.settings;
    const Json summary{{"schema_version", 1}, {"candidate", "SPY-MR-01"}, {"study_status", "synthetic_only"},
        {"historical_model_fits", 0}, {"historical_strategy_scores", 0}, {"trials_per_case", settings.trials},
        {"coverage_trials_per_case", settings.coverage_trials}, {"bootstrap_replicates", settings.bootstrap_replicates},
        {"seed", settings.seed}, {"status_order", statuses}, {"latent_equilibrium", simulation_equilibrium},
        {"innovation_sd", simulation_innovation_sd}, {"current_for_move", simulation_current}, {"entry_delay_minutes", 1},
        {"holding_minutes", 30}, {"rng", "mt19937_64; open-unit high-52-bit midpoint Box-Muller cosine; no cached variate"},
        {"seed_rule", "case seed = seed + case index * 1000003; refit seed = case seed + 500000003 + trial index"},
        {"range_convention", "linear 2.5/97.5 percentiles; OU ranges conditional on >=20 admissible refits"},
        {"cases", cases}, {"source_sha256", build::source_sha256}, {"compiler", TRADING_COMPILER_ID},
        {"build_configuration", TRADING_BUILD_CONFIGURATION}, {"artifacts", artifacts}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}
} // namespace trading
