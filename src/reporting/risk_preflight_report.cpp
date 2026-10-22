#include "risk_preflight_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {

std::string escape(std::string_view input) {
    std::string out;
    for (const auto c : input) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}
std::ostringstream stream() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    return out;
}

} // namespace

std::filesystem::path save_risk_preflight_report(const RiskPreflightResult& result,
    std::span<const CorporateActionProbe> probes, const std::string& access_status,
    const std::filesystem::path& output_root) {
    using Json = nlohmann::json;
    if (result.coverage.empty() || result.accounting.checks.empty() || result.blockers.empty()) {
        throw std::invalid_argument("preflight report requires coverage, accounting identities and unresolved prerequisites");
    }
    const auto directory = create_unique_directory(output_root, "SPY_RISK_01_preflight");
    Json summary{{"schema_version", 1}, {"kind", "data_and_synthetic_accounting_preflight"},
                 {"strategy_scored", false}, {"status", "blocked_before_registration"},
                 {"input_manifest_sha256", result.input_manifest_sha256},
                 {"blockers", result.blockers}, {"corporate_action_access", access_status},
                 {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID},
                 {"build_configuration", TRADING_BUILD_CONFIGURATION}};
    summary["calendar_snapshots"] = Json::array();
    for (const auto& [month, digest] : result.calendar_hashes) {
        summary["calendar_snapshots"].push_back({{"month", month}, {"file_sha256", digest}});
    }
    auto csv = stream();
    csv << "date,expected_minutes,observed_minutes,account_references_complete,rv5_endpoints_complete,missing_rv5_bar_starts_utc,canonical_bars_sha256,source\n";
    std::map<std::string, std::vector<const RiskSessionCoverage*>> months;
    std::size_t full = 0, account = 0, risk = 0;
    summary["sessions"] = Json::array();
    for (const auto& row : result.coverage) {
        months[row.date.substr(0, 7)].push_back(&row);
        full += row.observed_minutes == row.expected_minutes ? 1U : 0U;
        account += row.account_references_complete ? 1U : 0U;
        risk += row.rv5_endpoints_complete ? 1U : 0U;
        std::string missing;
        for (const auto& time : row.missing_rv5_bar_starts) { if (!missing.empty()) { missing += ';'; } missing += time; }
        csv << row.date << ',' << row.expected_minutes << ',' << row.observed_minutes << ','
            << row.account_references_complete << ',' << row.rv5_endpoints_complete << ','
            << missing << ',' << row.evidence_sha256 << ',' << row.source << '\n';
        summary["sessions"].push_back({{"date", row.date}, {"expected_minutes", row.expected_minutes},
            {"observed_minutes", row.observed_minutes}, {"account_references_complete", row.account_references_complete},
            {"rv5_endpoints_complete", row.rv5_endpoints_complete}, {"missing_rv5_bar_starts_utc", row.missing_rv5_bar_starts},
            {"canonical_bars_sha256", row.evidence_sha256}, {"source", row.source}});
    }
    auto ledger = stream();
    ledger << "timestamp_utc,event,action_id,raw_price_usd,traded_notional_usd,cost_usd,cash_usd,shares,receivable_usd,liquid_wealth_usd,equity_usd\n";
    for (const auto& row : result.accounting.ledger) {
        const auto& v = row.value;
        ledger << format_utc_seconds(row.at) << ',' << row.kind << ',' << row.action_id << ',' << row.reference_price_usd << ','
               << row.traded_notional_usd << ',' << row.cost_usd << ',' << v.cash_usd << ',' << v.shares << ','
               << v.receivable_usd << ',' << v.liquid_wealth_usd << ',' << v.equity_usd << '\n';
    }
    auto identities = stream();
    identities << "identity,actual,expected,absolute_tolerance,passed\n";
    summary["accounting_checks"] = Json::array();
    for (const auto& check : result.accounting.checks) {
        identities << check.name << ',' << check.actual << ',' << check.expected << ',' << check.tolerance << ',' << check.passed << '\n';
        summary["accounting_checks"].push_back({{"name", check.name}, {"actual", check.actual},
            {"expected", check.expected}, {"absolute_tolerance", check.tolerance}, {"passed", check.passed}});
    }
    summary["counts"] = {{"exchange_sessions", result.coverage.size()}, {"full_minute_grids", full},
                         {"account_reference_sessions", account}, {"rv5_endpoint_sessions", risk}};
    auto html = stream();
    html << R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SPY-RISK-01 | Data and accounting preflight</title><style>
:root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#edf1f5;color:#17283c;font:16px/1.55 system-ui,sans-serif}main{max-width:1180px;margin:auto;padding:44px 28px}h1{font-size:34px;line-height:1.2;margin:10px 0}h2{margin:36px 0 12px;font-size:23px}p{max-width:90ch}.eyebrow{color:#376282;font-size:13px;letter-spacing:.1em;text-transform:uppercase}.status{display:inline-block;padding:5px 12px;background:#fff0d1;color:#77460d;border-radius:5px;font-weight:650}.tiles{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin:24px 0}.tile,section{background:white;border:1px solid #d5dee7;border-radius:8px;padding:20px}.tile strong{font-size:30px;display:block}.tile span,.note{color:#50657a;font-size:14px}section{margin:18px 0}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;font-size:14px}th{text-align:left;color:#37546d;background:#f2f6f9}th,td{padding:10px 12px;border-bottom:1px solid #e0e7ee}td.num{text-align:right;font-variant-numeric:tabular-nums}.mono{font-family:ui-monospace,monospace;overflow-wrap:anywhere}svg{width:100%;min-width:650px;height:auto}ul{padding-left:22px}a{color:#205c91}@media(max-width:700px){.tiles{grid-template-columns:repeat(2,1fr)}main{padding:24px 16px}h1{font-size:28px}}</style></head><body><main>
<div class="eyebrow">SPY-RISK-01 / feasibility checkpoint</div><h1>Data coverage and overnight accounting</h1>
<p class="status">Blocked before registration</p><p>Observed input coverage and constructed account identities. No strategy, risk forecast or historical account performance is calculated.</p><div class="tiles">
)";
    const auto total = std::to_string(result.coverage.size());
    for (const auto& [value, label] : std::vector<std::pair<std::string, std::string>>{
        {total, "Exchange sessions, 2022–2023"}, {std::to_string(full) + "/" + total, "Complete minute grids"},
        {std::to_string(account) + "/" + total, "Account reference sets"}, {std::to_string(risk) + "/" + total, "Five-minute endpoint sets"}}) {
        html << "<div class=\"tile\"><strong>" << value << "</strong><span>" << label << "</span></div>";
    }
    html << "</div><h2>Observed calendar coverage</h2><section><p class=\"note\">One square per exchange session, in date order within each month. Green: required account and RV5 references present. Amber: retained data with missing references. Red: no verified session evidence. Complete endpoint sets do not imply complete minute grids.</p><div class=\"scroll\">";
    html << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 455\" role=\"img\"><title>Required reference coverage on all exchange sessions</title>";
    std::size_t m = 0;
    for (const auto& [month, rows] : months) {
        const auto x = static_cast<int>(m / 12) * 500 + 10;
        const auto y = static_cast<int>(m % 12) * 36 + 24;
        html << "<text x=\"" << x << "\" y=\"" << y << "\" font-size=\"13\" fill=\"#37546d\">" << month << "</text>";
        std::size_t i = 0;
        for (const auto* row : rows) {
            const auto color = row->observed_minutes == 0 ? "#b64848" :
                row->account_references_complete && row->rv5_endpoints_complete ? "#287c73" : "#d09529";
            html << "<rect x=\"" << x + 82 + static_cast<int>(i) * 16 << "\" y=\"" << y - 13
                 << "\" width=\"12\" height=\"16\" rx=\"2\" fill=\"" << color << "\"><title>"
                 << row->date << ": " << row->observed_minutes << '/' << row->expected_minutes << " bars</title></rect>";
            ++i;
        }
        ++m;
    }
    html << "</svg></div><p>Account references: session open, 09:31 and 09:32 minute opens, and the last regular-session minute close. RV5: open and each five-minute interval close, including shortened sessions. The preceding close and corporate actions are additional requirements.</p></section>";
    html << "<h2>Unresolved session coverage</h2><section class=\"scroll\"><table><tr><th>Date</th><th>Bars</th><th>Account references</th><th>RV5 endpoints</th><th>Missing endpoint bar starts (UTC)</th></tr>";
    for (const auto& row : result.coverage) {
        if (row.observed_minutes == row.expected_minutes) { continue; }
        html << "<tr><td>" << row.date << "</td><td>" << row.observed_minutes << '/' << row.expected_minutes << "</td><td>"
             << (row.account_references_complete ? "Present" : "Missing") << "</td><td>"
             << (row.rv5_endpoints_complete ? "Present" : "Missing") << "</td><td>";
        if (row.observed_minutes == 0) { html << "No retained session evidence"; }
        else { for (const auto& time : row.missing_rv5_bar_starts) { html << escape(time) << "<br>"; } }
        html << "</td></tr>";
    }
    html << "</table><p class=\"note\">The original exclusions remain unchanged. Excluding a session from an intraday study does not remove overnight economic exposure from a proposed account.</p></section><h2>Corporate-action access</h2><section><p>"
         << escape(access_status) << "</p><table><tr><th>Process-date year</th><th>HTTP</th><th>JSON object</th><th>More pages</th></tr>";
    summary["corporate_action_probes"] = Json::array();
    std::vector<std::string> files{"coverage.csv", "synthetic_ledger.csv", "accounting_checks.csv", "report.html"};
    for (const auto& probe : probes) {
        html << "<tr><td>" << probe.year << "</td><td>" << probe.http_status << "</td><td>" << (probe.json_object ? "Yes" : "No")
             << "</td><td>" << (probe.more_pages ? "Yes" : "No") << "</td></tr>";
        const auto file = "corporate_actions_" + std::to_string(probe.year) + "_response.txt";
        if (sha256(probe.body) != probe.body_sha256) { throw std::invalid_argument("corporate-action probe response hash mismatch"); }
        write_new_text_file(directory / file, probe.body);
        files.push_back(file);
        summary["corporate_action_probes"].push_back({{"year", probe.year}, {"request_path", probe.request_path},
            {"http_status", probe.http_status}, {"body_sha256", probe.body_sha256}, {"file", file},
            {"json_object", probe.json_object}, {"more_pages", probe.more_pages},
            {"retrieved_utc", probe.retrieved_utc},
            {"endpoint", "https://data.alpaca.markets/v1/corporate-actions"}});
    }
    html << R"(</table><p>A successful request establishes endpoint access only. This bounded probe retains one page per year, with all action types and <code>data_quality=complete</code>. Pagination, ex-date coverage, pay dates, distributions and independent totals require reconciliation. Process-date filters do not guarantee ex-date coverage; current historical records do not establish point-in-time announcement availability.</p></section>
<h2>Constructed accounting identities</h2><section><p>Fractional long/cash USD account, zero cash interest, no borrowing. The constructed trace starts at USD 10,000, buys a 50% post-cost weight at USD 100 with 1 bp cost, accrues a USD 1 dividend at a raw price of USD 99, sells the stock and later receives payment. These values are not market observations.</p><div class="scroll"><table><tr><th>Identity</th><th>Actual</th><th>Expected</th><th>Result</th></tr>)";
    html << std::fixed << std::setprecision(9);
    for (const auto& check : result.accounting.checks) {
        html << "<tr><td>" << escape(check.name) << "</td><td class=\"num\">" << check.actual
             << "</td><td class=\"num\">" << check.expected << "</td><td>" << (check.passed ? "Pass" : "Fail") << "</td></tr>";
    }
    html << "</table></div><p class=\"note\">Absolute tolerance: 1e-9 in the displayed identity's units. The split identities use a separate constructed account. This verifies the stated arithmetic, not settlement, tax, brokerage or production readiness.</p><div class=\"scroll\"><table><tr><th>UTC date</th><th>Event</th><th>Cash (USD)</th><th>Stock shares</th><th>Receivable (USD)</th><th>Equity (USD)</th></tr>";
    html << std::setprecision(4);
    for (const auto& row : result.accounting.ledger) {
        html << "<tr><td>" << format_utc_seconds(row.at) << "</td><td>" << escape(row.kind) << "</td>";
        for (const auto v : {row.value.cash_usd, row.value.shares, row.value.receivable_usd, row.value.equity_usd}) {
            html << "<td class=\"num\">" << v << "</td>";
        }
        html << "</tr>";
    }
    html << "</table></div></section><h2>Required before registration and scoring</h2><section><ul>";
    for (const auto& blocker : result.blockers) { html << "<li>" << escape(blocker) << "</li>"; }
    html << "</ul></section><h2>Reproduction records</h2><section><p>Verified input manifest SHA-256:</p><p class=\"mono\">"
         << escape(result.input_manifest_sha256) << "</p><p>Source SHA-256:</p><p class=\"mono\">" << TRADING_SOURCE_SHA256
         << "</p><p>Compiler: " << escape(TRADING_COMPILER_ID) << " / " << escape(TRADING_BUILD_CONFIGURATION)
         << "</p><p><a href=\"coverage.csv\">Session coverage</a> · <a href=\"synthetic_ledger.csv\">Constructed ledger</a> · <a href=\"accounting_checks.csv\">Accounting identities</a> · <a href=\"summary.json\">Manifest and provenance</a></p></section></main></body></html>";
    write_new_text_file(directory / "coverage.csv", csv.str());
    write_new_text_file(directory / "synthetic_ledger.csv", ledger.str());
    write_new_text_file(directory / "accounting_checks.csv", identities.str());
    write_new_text_file(directory / "report.html", html.str());
    summary["artifacts"] = Json::array();
    for (const auto& file : files) { summary["artifacts"].push_back({{"file", file}, {"sha256", sha256_file(directory / file)}}); }
    summary["created_utc"] = format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
    return directory;
}

} // namespace trading
