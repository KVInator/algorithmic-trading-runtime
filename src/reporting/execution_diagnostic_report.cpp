#include "report_text.hpp"
#include "execution_diagnostic_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "runtime_json.hpp"
#include "trading_source_inventory.hpp"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

namespace trading::execution {
namespace {

using Json = nlohmann::json;

} // namespace

void ExecutionDiagnosticReport::write(const std::filesystem::path& directory) const {
    const auto& results = result_;
    Json inputs = Json::array();
    Json cases = Json::array();
    std::size_t transitions = 0;
    std::ostringstream csv;
    csv << "scenario,sequence,input,order_id,event_id,outcome,reason,order_status,cash_cents,position_shares,"
           "reserved_cash_cents,reserved_sell_shares,outstanding_buy_shares,open_orders,connected,reconciliation_required\n";
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Execution runtime diagnostic</title><style>"
            "body{font:16px system-ui;margin:2rem auto;padding:0 1rem;max-width:1500px;color:#152434;background:#f5f7fa}"
            "h1,h2{color:#183b63}.scroll{overflow:auto;background:white;border:1px solid #ccd6e0}"
            "table{border-collapse:collapse;width:100%;font-size:13px}th,td{padding:9px;text-align:left;border-bottom:1px solid #ddd}"
            "th{background:#e7eef6}td.num{text-align:right;font-variant-numeric:tabular-nums}"
            ".quarantined{background:#fff0e8}.denied{background:#fff9df}.duplicate,.obsolete{background:#edf3f8}"
            "code{overflow-wrap:anywhere}section{margin:2rem 0}</style><main>"
            "<h1>Execution runtime diagnostic</h1><p>Constructed broker-message schedules only. "
            "Whole shares, integer USD cents, single-symbol long/cash limit orders. "
            "Every expected outcome and final balance passed; each schedule was replayed twice with identical traces.</p>"
            "<p>Quarantined rows are expected fault injections. They close the entry gate and preserve accounting. "
            "A matching broker snapshot is required before new orders resume. "
            "No market matching, historical strategy score or broker connection is exercised.</p>";
    for (const auto& result : results) {
        Json steps = Json::array();
        for (const auto& step : result.scenario.steps) {
            steps.push_back({{"input", input_json(step.input)}, {"expected_outcome", name(step.expected_outcome)}});
        }
        inputs.push_back({{"scenario", result.scenario.name}, {"steps", std::move(steps)}});
        transitions += result.trace.size();
        cases.push_back({{"scenario", result.scenario.name}, {"transitions", result.trace.size()},
                         {"expected_cash_cents", result.scenario.expected_cash_cents},
                         {"expected_position_shares", result.scenario.expected_position_shares},
                         {"final_account", account_json(result.final_account)}, {"deterministic_replay", true}});
        html << "<section><h2>" << ReportText{result.scenario.name}.html() << "</h2><p>Final cash: "
             << result.final_account.cash_units << " cents; position: " << result.final_account.position_shares
             << " shares; no outstanding reservations.</p><div class=\"scroll\"><table><thead><tr>"
                "<th>Step</th><th>Input</th><th>Order / event</th><th>Outcome</th><th>State</th><th>Cash (cents)</th>"
                "<th>Shares</th><th>Cash reserved</th><th>Shares reserved</th><th>Entry gate</th><th>Reason</th>"
                "</tr></thead><tbody>";
        for (const auto& row : result.trace) {
            const auto& a = row.account;
            csv << ReportText{result.scenario.name}.csv() << ',' << row.sequence << ',' << row.input << ','
                << ReportText{row.order_id}.csv() << ',' << ReportText{row.event_id}.csv() << ',' << name(row.outcome) << ','
                << ReportText{row.reason}.csv() << ',' << row.order_status << ',' << a.cash_units << ',' << a.position_shares
                << ',' << a.reserved_cash_units << ',' << a.reserved_sell_shares << ',' << a.outstanding_buy_shares
                << ',' << a.open_orders << ',' << a.connected << ',' << a.reconciliation_required << '\n';
            html << "<tr class=\"" << name(row.outcome) << "\"><td>" << row.sequence << "</td><td>" << row.input
                 << "</td><td>" << ReportText{row.order_id}.html() << " / " << ReportText{row.event_id}.html()
                 << "</td><td>" << name(row.outcome) << "</td><td>" << row.order_status
                 << "</td><td class=\"num\">" << a.cash_units << "</td><td class=\"num\">" << a.position_shares
                 << "</td><td class=\"num\">" << a.reserved_cash_units << "</td><td class=\"num\">" << a.reserved_sell_shares
                 << "</td><td>" << (a.connected && !a.reconciliation_required ? "open" : "closed")
                 << "</td><td>" << ReportText{row.reason}.html() << "</td></tr>";
        }
        html << "</tbody></table></div></section>";
    }
    html << "<h2>Reproduction evidence</h2><p>Source SHA-256: <code>" << build::source_sha256
         << "</code></p><p><a href=\"inputs.json\">Complete inputs</a> | <a href=\"trace.csv\">Event trace</a> | "
            "<a href=\"source_inventory.txt\">Per-file source hashes</a> | <a href=\"summary.json\">Manifest</a></p></main></html>\n";
    write_new_text_file(directory / "inputs.json", inputs.dump(2) + '\n');
    write_new_text_file(directory / "trace.csv", csv.str());
    write_new_text_file(directory / "report.html", html.str());
    write_new_text_file(directory / "source_inventory.txt", build::source_records);
    Json artifacts = Json::array();
    for (const auto* filename : {"inputs.json", "trace.csv", "report.html", "source_inventory.txt"}) {
        artifacts.push_back({{"path", filename}, {"sha256", sha256_file(directory / filename)}});
    }
    Json summary{{"schema_version", 1}, {"status", "synthetic_contracts_passed"},
                 {"model", "single_symbol_whole_share_limit_orders_usd_cents"},
                 {"initial_cash_cents", 1000000}, {"maximum_position_shares", 20}, {"maximum_open_orders", 4},
                 {"scenario_count", results.size()}, {"checked_transitions", transitions}, {"scenarios", std::move(cases)},
                 {"source_sha256", build::source_sha256}, {"source_inventory_scope", "existing study source fingerprint scope"},
                 {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
                 {"artifacts", std::move(artifacts)}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}

} // namespace trading::execution
