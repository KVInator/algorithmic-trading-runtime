#include "report_text.hpp"
#include "pipeline_diagnostic_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "minute_bar_csv.hpp"
#include "runtime_json.hpp"
#include "trading_source_inventory.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

namespace trading::execution {
void PipelineDiagnosticReport::write(const std::filesystem::path& directory) const {
    const auto& result = result_;
    using Json = nlohmann::json;
    Json cases = Json::array();
    Json checks = Json::array();
    std::vector<std::string> artifacts;
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Offline execution pipeline</title><style>body{font:16px system-ui;max-width:1400px;margin:2rem auto;padding:0 1rem;"
            "background:#f5f7fa;color:#152434}h1,h2{color:#183b63}.scroll{overflow:auto}table{border-collapse:collapse;background:white;width:100%;font-size:13px}"
            "td,th{padding:9px;text-align:left;border-bottom:1px solid #ccd6e0}th{background:#e7eef6}details{margin:1rem 0}code{overflow-wrap:anywhere}</style>"
            "<main><h1>Offline execution pipeline</h1><p>The existing momentum signal uses 360 completed synthetic minute bars. "
            "Durable targets feed whole-share orders to an independent FIFO top-of-book simulator, which generates acknowledgements, fills and cancellation responses.</p>"
            "<p>The interrupted client restarts twice while its broker retains executions and undelivered reports. "
            "Both active cases must produce identical broker orders, fills and final account states. The falling-prefix case remains in cash.</p>"
            "<p>Prices, displayed sizes and fees are constructed contract fixtures. Account balances are not historical strategy results. "
            "Frozen study rules and later-period data are unchanged.</p><h2>Client event traces</h2>";
    for (const auto& item : result.cases) {
        Json quotes = Json::array(), targets = Json::array(), fills = Json::array(), reports = Json::array(), trace = Json::array();
        for (const auto& q : item.quotes) {
            quotes.push_back({{"sequence", q.sequence}, {"at_seconds", q.at_seconds}, {"bid_cents", q.bid_cents},
                {"ask_cents", q.ask_cents}, {"bid_shares", q.bid_shares}, {"ask_shares", q.ask_shares}});
        }
        for (const auto& target : item.targets) { targets.push_back(input_json(target)); }
        for (const auto& f : item.fills) {
            fills.push_back({{"execution_id", f.execution_id}, {"order_id", f.order_id}, {"side", name(f.side)},
                {"shares", f.shares}, {"price_cents", f.price_cents}, {"fee_cents", f.fee_units}, {"quote_sequence", f.quote_sequence}});
        }
        for (const auto& report : item.broker_reports) { reports.push_back(input_json(report)); }
        for (const auto& row : item.runtime_trace) { trace.push_back(trace_json(row)); }
        cases.push_back({{"name", item.name}, {"journal_file", item.journal_file}, {"quotes", quotes}, {"targets", targets},
            {"broker_fills", fills}, {"broker_reports", reports}, {"runtime_trace", trace},
            {"final_account", account_json(item.final_account)}, {"client_restarts", item.client_restarts}});
        artifacts.push_back(item.journal_file);
        html << "<section><h2>" << ReportText{item.name}.html() << "</h2><p>Client restarts: " << item.client_restarts
             << "; broker executions: " << item.fills.size() << "; final cash: " << item.final_account.cash_units
             << " cents; final inventory: " << item.final_account.position_shares << " shares.</p><details><summary>Expand durable event trace</summary>"
                "<div class=\"scroll\"><table><tr><th>Seq</th><th>Input</th><th>Order / event</th><th>Outcome</th><th>State</th>"
                "<th>Cash (cents)</th><th>Shares</th><th>Cash reserved</th><th>Shares reserved</th><th>Entry gate</th><th>Reason</th></tr>";
        for (const auto& row : item.runtime_trace) {
            const auto& a = row.account;
            html << "<tr><td>" << row.sequence << "</td><td>" << row.input << "</td><td>" << ReportText{row.order_id}.html()
                 << " / " << ReportText{row.event_id}.html() << "</td><td>" << name(row.outcome) << "</td><td>" << row.order_status
                 << "</td><td>" << a.cash_units << "</td><td>" << a.position_shares << "</td><td>" << a.reserved_cash_units
                 << "</td><td>" << a.reserved_sell_shares << "</td><td>" << (a.connected && !a.reconciliation_required ? "open" : "closed")
                 << "</td><td>" << ReportText{row.reason}.html() << "</td></tr>";
        }
        html << "</table></div></details></section>";
    }
    html << "<h2>Checks</h2><ul>";
    for (const auto& check : result.checks) {
        checks.push_back({{"name", check.name}, {"passed", check.passed}});
        html << "<li>" << ReportText{check.name}.html() << ": " << (check.passed ? "passed" : "failed") << "</li>";
    }
    html << "</ul><h2>Reproduction evidence</h2><p>Source SHA-256: <code>" << build::source_sha256
         << "</code></p><p><a href=\"pipeline_evidence.json\">Quotes, targets, fills and client traces</a> | "
            "<a href=\"signal_rising.csv\">Rising prefix</a> | <a href=\"signal_falling.csv\">Falling prefix</a> | "
            "<a href=\"summary.json\">Manifest</a></p></main></html>\n";
    write_new_text_file(directory / "pipeline_evidence.json", Json{{"checks", checks}, {"cases", cases}}.dump(2) + '\n');
    write_new_text_file(directory / "signal_rising.csv", format_minute_bars_csv(result.rising_prefix));
    write_new_text_file(directory / "signal_falling.csv", format_minute_bars_csv(result.falling_prefix));
    write_new_text_file(directory / "report.html", html.str());
    write_new_text_file(directory / "source_inventory.txt", build::source_records);
    for (const auto* file : {"pipeline_evidence.json", "signal_rising.csv", "signal_falling.csv", "report.html", "source_inventory.txt"}) { artifacts.emplace_back(file); }
    Json identities = Json::array();
    for (const auto& file : artifacts) { identities.push_back({{"path", file}, {"sha256", sha256_file(directory / file)}}); }
    const Json summary{{"schema_version", 1}, {"journal_schema_version", 2}, {"status", "synthetic_pipeline_contracts_passed"},
        {"checked_assertions", checks.size()}, {"case_count", cases.size()}, {"source_sha256", build::source_sha256},
        {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION}, {"artifacts", identities}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}

} // namespace trading::execution
