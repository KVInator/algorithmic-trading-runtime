#include "risk_input_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
std::string escape(std::string_view value) {
    std::string out;
    for (const auto c : value) {
        if (c == '&') { out += "&amp;"; } else if (c == '<') { out += "&lt;"; }
        else if (c == '>') { out += "&gt;"; } else if (c == '"') { out += "&quot;"; } else { out += c; }
    }
    return out;
}
std::string csv_cell(const std::string& value) {
    std::string result = "\"";
    for (const auto c : value) { if (c == '"') { result += '"'; } result += c; }
    return result + '"';
}
} // namespace

std::filesystem::path save_risk_input_report(const RiskInputInventory& inputs,
    const RiskPreflightResult& preflight, const std::filesystem::path& output_root) {
    using Json = nlohmann::json;
    if (inputs.prices.empty() || preflight.coverage.empty()) { throw std::invalid_argument("risk input report requires audited inputs"); }
    const auto directory = create_unique_directory(output_root, "SPY_RISK_01_inputs");
    Json summary{{"schema_version", 1}, {"kind", "risk_input_acquisition_and_coverage"},
        {"status", "blocked_before_registration"}, {"strategy_scored", false},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID},
        {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"frozen_study_manifest_sha256", preflight.input_manifest_sha256}, {"blockers", inputs.blockers},
        {"remaining_preflight_blockers", preflight.blockers}};
    summary["issuer_reference_sha256"] = inputs.issuer_reference_sha256;
    summary["cash_distributions_reconciled"] = inputs.cash_distributions_reconciled;
    summary["price_inputs"] = Json::array();
    summary["evidence"] = Json::array();
    summary["cash_distributions"] = Json::array();
    summary["development_coverage"] = Json::array();
    std::size_t complete = 0;
    std::ostringstream prices, dividends, coverage, html;
    for (auto* stream : {&prices, &dividends, &coverage, &html}) { stream->imbue(std::locale::classic()); *stream << std::setprecision(17); }
    prices << "date,role,expected_minutes,observed_minutes,evidence_sha256\n";
    for (const auto& row : inputs.prices) {
        const auto expected = row.input.request.expected_minutes();
        complete += row.observed_minutes == expected ? 1U : 0U;
        prices << row.input.date << ',' << row.input.role << ',' << expected << ',' << row.observed_minutes << ',' << row.evidence_sha256 << '\n';
        summary["price_inputs"].push_back({{"date", row.input.date}, {"role", row.input.role},
            {"expected_minutes", expected}, {"observed_minutes", row.observed_minutes}, {"evidence_sha256", row.evidence_sha256}});
    }
    for (const auto& [file, digest] : inputs.evidence_hashes) { summary["evidence"].push_back({{"file", file}, {"sha256", digest}}); }
    dividends << "id,ex_date,record_date,payable_date,process_date,usd_per_share,in_2021_2023_ex_date_horizon\n";
    for (const auto& row : inputs.actions.distributions()) {
        dividends << csv_cell(row.id) << ',' << row.ex_date << ',' << row.record_date << ',' << row.payable_date << ','
            << row.process_date << ',' << row.usd_per_share << ',' << (row.ex_date >= "2021-01-01" && row.ex_date <= "2023-12-31") << '\n';
        summary["cash_distributions"].push_back({{"id", row.id}, {"ex_date", row.ex_date}, {"record_date", row.record_date},
            {"payable_date", row.payable_date}, {"process_date", row.process_date}, {"usd_per_share", row.usd_per_share}});
    }
    coverage << "date,expected_minutes,observed_minutes,account_references_complete,rv5_endpoints_complete\n";
    std::size_t account = 0, risk = 0;
    for (const auto& row : preflight.coverage) {
        account += row.account_references_complete ? 1U : 0U; risk += row.rv5_endpoints_complete ? 1U : 0U;
        coverage << row.date << ',' << row.expected_minutes << ',' << row.observed_minutes << ',' << row.account_references_complete << ',' << row.rv5_endpoints_complete << '\n';
        summary["development_coverage"].push_back({{"date", row.date}, {"account_references_complete", row.account_references_complete},
            {"rv5_endpoints_complete", row.rv5_endpoints_complete}, {"canonical_bars_sha256", row.evidence_sha256}});
    }
    summary["counts"] = {{"planned_price_inputs", inputs.prices.size()}, {"complete_price_inputs", complete},
        {"development_exchange_sessions", preflight.coverage.size()}, {"account_reference_sessions", account}, {"rv5_endpoint_sessions", risk}};
    html << R"(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SPY-RISK-01 | Input acquisition</title>
<style>body{background:#edf1f5;color:#17283c;font:16px/1.6 system-ui;margin:0}main{max-width:1150px;margin:auto;padding:36px 24px}section{background:white;padding:24px;margin:20px 0;border-radius:8px;overflow:auto}table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:9px;text-align:left;border-bottom:1px solid #d5dee7}th{background:#f2f6f9}h1{line-height:1.2}.status{color:#77460d}a{color:#205c91}</style><main>
<h1>Daily-risk input acquisition</h1><p class="status">Blocked before registration</p><p>Calendar-verified price references and present-day corporate-action evidence. No risk forecast, allocation weight or strategy performance is calculated.</p>)";
    html << "<section><h2>Coverage</h2><p>Complete planned price inputs: " << complete << '/' << inputs.prices.size()
         << ". Development account references: " << account << '/' << preflight.coverage.size()
         << ". Development RV5 endpoint sets: " << risk << '/' << preflight.coverage.size() << ".</p>"
         << "<p>The plan contains one preceding close, 252 calibration dates with the final twenty retained in full, and three shortened development sessions. Raw one-minute closing references are proxies for the final regular-session minute, not official auction closes.</p></section>"
         << "<section><h2>Cash-distribution records</h2><p>Process-date requests extend through 31 March 2024 to collect late-processed earlier entitlements. These requests contain no price or strategy-return observations.</p><p>Cash distributions agree with the twelve-record issuer reference: "
         << (inputs.cash_distributions_reconciled ? "Yes" : "No")
         << ". Matching ex-dates, payment dates and amounts does not establish point-in-time announcement availability.</p><table><tr><th>Ex-date</th><th>Payable date</th><th>Process date</th><th>USD/share</th></tr>";
    for (const auto& row : inputs.actions.distributions()) { html << "<tr><td>" << row.ex_date << "</td><td>" << row.payable_date << "</td><td>" << row.process_date << "</td><td>" << row.usd_per_share << "</td></tr>"; }
    html << "</table></section><section><h2>Required before registration</h2><ul>";
    for (const auto& blocker : inputs.blockers) { html << "<li>" << escape(blocker) << "</li>"; }
    html << "</ul><p>Earlier preflight prerequisites remain separately recorded in the JSON. Verified calibration acquisition does not itself resolve action reconciliation or the missing-risk policy.</p></section><section><h2>Evidence</h2><p><a href=\"price_inputs.csv\">Price inventory</a> · <a href=\"cash_distributions.csv\">Cash distributions</a> · <a href=\"coverage.csv\">Development coverage</a> · <a href=\"summary.json\">Manifest</a></p></section></main></html>";
    write_new_text_file(directory / "price_inputs.csv", prices.str());
    write_new_text_file(directory / "cash_distributions.csv", dividends.str());
    write_new_text_file(directory / "coverage.csv", coverage.str());
    write_new_text_file(directory / "report.html", html.str());
    summary["artifacts"] = Json::array();
    for (const auto* file : {"price_inputs.csv", "cash_distributions.csv", "coverage.csv", "report.html"}) {
        summary["artifacts"].push_back({{"file", file}, {"sha256", sha256_file(directory / file)}});
    }
    summary["created_utc"] = format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
    return directory;
}
} // namespace trading
