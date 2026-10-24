#include "report_text.hpp"
#include "reversion_preflight_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "minute_bar_csv.hpp"
#include "trading_source_inventory.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;
Json number(const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); }
} // namespace

void ReversionPreflightReport::write(const std::filesystem::path& directory) const {
    const auto& result = result_;
    Json controls = Json::array(), checks = Json::array();
    std::ostringstream coverage, html;
    coverage << "date,earlier_training_sessions,complete_session,complete_prefix,positive_anchor_volume,reference_clocks_present,input_ready\n";
    std::size_t ready = 0;
    for (const auto& row : result.coverage) {
        ready += row.input_ready ? 1U : 0U;
        coverage << row.date << ',' << row.earlier_training_sessions << ',' << row.complete_session << ','
            << row.complete_prefix << ',' << row.positive_anchor_volume << ',' << row.reference_clocks_present << ',' << row.input_ready << '\n';
    }
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Opening-anchor reversion preflight</title><style>body{font:16px system-ui;max-width:1150px;margin:2rem auto;padding:0 1rem;"
        "color:#192c40;background:#f6f8fb}table{border-collapse:collapse;width:100%;background:white}th,td{padding:10px;text-align:left;"
        "border-bottom:1px solid #ccd7e2}th{background:#e8eff7}.scroll{overflow:auto}code{overflow-wrap:anywhere}</style>"
        "<main><h1>Opening-anchor reversion preflight</h1><p>Revision 1 input-feasibility audit. This report contains no historical model evaluation.</p>"
        "<p>The opening reference is fixed from the first thirty minutes. Five-minute closing residuals end at noon. "
        "The estimator fits within-session AR(1) transitions without connecting session boundaries.</p>"
        "<p>All parameter fits below use constructed controls. Historical coverage checks inspect timestamps and positive opening volume only. "
        "No historical residual, parameter, forecast, trading decision or return is calculated.</p>"
        "<h2>Synthetic parameter controls</h2><div class=\"scroll\"><table><tr><th>Control</th><th>Declared slope</th>"
        "<th>Estimated slope</th><th>Interpretation</th><th>Half-life (minutes)</th></tr>";
    for (const auto& item : result.controls) {
        const auto& f = item.fitted;
        controls.push_back({{"label", item.label}, {"declared_intercept", item.declared_intercept}, {"declared_slope", item.declared_slope},
            {"sequences", item.sequences}, {"pairs", f.pairs}, {"status", name(f.status)}, {"intercept", number(f.intercept)},
            {"slope", number(f.slope)}, {"innovation_variance", number(f.innovation_variance)}, {"equilibrium", number(f.equilibrium)},
            {"speed_per_minute", number(f.speed_per_minute)}, {"half_life_minutes", number(f.half_life_minutes)}});
        html << "<tr><td>" << ReportText{item.label}.html() << "</td><td>" << item.declared_slope << "</td><td>"
            << (f.slope ? std::to_string(*f.slope) : "undefined") << "</td><td>" << name(f.status) << "</td><td>"
            << (f.half_life_minutes ? std::to_string(*f.half_life_minutes) : "undefined") << "</td></tr>";
    }
    html << "</table></div><p>Unrestricted coefficients remain visible. A sub-unit coefficient is a model condition, "
        "not an empirical stationarity test. Synthetic agreement does not establish reversion in SPY.</p>"
        "<h2>Input coverage</h2><p>Sessions audited: " << result.coverage.size() << ". Input-ready dates after warm-up: " << ready
        << ". Green indicates structural availability, blue warm-up and red an unavailable prerequisite.</p>"
        "<svg viewBox=\"0 0 690 370\" role=\"img\" aria-label=\"Session input availability\">";
    for (std::size_t i = 0; i < result.coverage.size(); ++i) {
        const auto& row = result.coverage[i];
        const bool available = row.complete_session && row.complete_prefix && row.positive_anchor_volume && row.reference_clocks_present;
        html << "<rect x=\"" << 8 + i % 31 * 22 << "\" y=\"" << 8 + i / 31 * 22 << "\" width=\"17\" height=\"17\" fill=\""
            << (row.input_ready ? "#27856a" : available ? "#537ba8" : "#b44747")
            << "\"><title>" << row.date << "</title></rect>";
    }
    html << "</svg><p>The full audit is retained in <a href=\"coverage.csv\">coverage.csv</a>. Availability does not establish parameter "
        "stability, live information timing or economic value.</p><h2>Constructed checks</h2><ul>";
    for (const auto& check : result.checks) {
        checks.push_back({{"label", check.label}, {"passed", check.passed}});
        html << "<li>" << ReportText{check.label}.html() << ": " << (check.passed ? "passed" : "failed") << "</li>";
    }
    html << "</ul><p><a href=\"controls.json\">Constructed inputs and fitted parameters</a> | "
        "<a href=\"synthetic_prefix.csv\">Exact synthetic minute prefix</a> | <a href=\"summary.json\">Manifest</a></p>"
        "<p>Source SHA-256: <code>" << build::source_sha256 << "</code></p></main></html>\n";
    const Json data{{"controls", controls}, {"checks", checks}, {"synthetic_anchor_price", result.synthetic_measurement.anchor_price},
        {"synthetic_residuals", result.synthetic_measurement.values}};
    write_new_text_file(directory / "controls.json", data.dump(2) + '\n');
    write_new_text_file(directory / "coverage.csv", coverage.str());
    write_new_text_file(directory / "synthetic_prefix.csv", format_minute_bars_csv(result.synthetic_prefix));
    write_new_text_file(directory / "source_inventory.txt", build::source_records);
    write_new_text_file(directory / "report.html", html.str());
    Json artifacts = Json::array();
    for (const auto* file : {"controls.json", "coverage.csv", "synthetic_prefix.csv", "source_inventory.txt", "report.html"}) {
        artifacts.push_back({{"path", file}, {"sha256", sha256_file(directory / file)}});
    }
    const Json summary{{"schema_version", 1}, {"candidate", "SPY-MR-01"}, {"study_status", "registered_unscored"},
        {"historical_model_fits", 0}, {"historical_strategy_scores", 0}, {"constructed_checks", result.checks.size()},
        {"synthetic_control_cases", result.controls.size()}, {"audited_sessions", result.coverage.size()}, {"input_ready_dates", ready},
        {"coverage_status", !result.study_manifest_sha256 ? "not_audited" :
            result.coverage.size() == 496 && ready == 476 ? "available_before_historical_evaluation" : "incomplete_prerequisites"},
        {"historical_inputs_verified", result.study_manifest_sha256.has_value()},
        {"study_manifest_sha256", result.study_manifest_sha256 ? Json(*result.study_manifest_sha256) : Json(nullptr)},
        {"source_sha256", build::source_sha256}, {"compiler", TRADING_COMPILER_ID},
        {"build_configuration", TRADING_BUILD_CONFIGURATION}, {"artifacts", artifacts}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}

} // namespace trading
