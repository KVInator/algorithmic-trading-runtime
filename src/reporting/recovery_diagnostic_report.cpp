#include "recovery_diagnostic_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "runtime_json.hpp"
#include "trading_source_inventory.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

namespace trading::execution {

void RecoveryDiagnosticReport::write(const std::filesystem::path& directory) const {
    const auto& result = result_;
    using Json = nlohmann::json;
    Json checks = Json::array();
    Json checkpoints = Json::array();
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Journal recovery diagnostic</title><style>body{font:16px system-ui;max-width:1300px;margin:2rem auto;padding:0 1rem;"
            "background:#f5f7fa;color:#152434}h1,h2{color:#183b63}.scroll{overflow:auto}table{border-collapse:collapse;background:white;width:100%}"
            "td,th{padding:10px;text-align:left;border-bottom:1px solid #ccd6e0}th{background:#e7eef6}code{overflow-wrap:anywhere}</style>"
            "<main><h1>Journal recovery diagnostic</h1><p>Synthetic whole-share orders and integer USD cents. "
            "Real journal files are synchronized, closed, reopened and replayed through the order core. "
            "Recovery closes the entry gate before returning.</p><p>Incomplete final frames require a separate verified-prefix file. "
            "Complete-frame corruption is rejected. Original fault evidence remains unchanged.</p><h2>Recovered states</h2>"
            "<div class=\"scroll\"><table><tr><th>Case / stage</th><th>Order</th><th>Cash (cents)</th><th>Shares</th>"
            "<th>Cash reserved</th><th>Filled</th><th>Entry gate</th><th>Records</th></tr>";
    for (const auto& row : result.checkpoints) {
        const auto& a = row.account;
        checkpoints.push_back({{"scenario", row.scenario}, {"stage", row.stage}, {"account", account_json(a)},
            {"order_status", name(row.order.status)}, {"filled_shares", row.order.filled_shares},
            {"pending_cancel_id", row.order.pending_cancel_id}, {"journal_records", row.journal_records}});
        // Labels originate in the fixed diagnostic, never in broker messages.
        html << "<tr><td>" << row.scenario << "<br>" << row.stage << "</td><td>" << name(row.order.status)
             << "</td><td>" << a.cash_units << "</td><td>" << a.position_shares << "</td><td>" << a.reserved_cash_units
             << "</td><td>" << row.order.filled_shares << "</td><td>" << (a.connected && !a.reconciliation_required ? "open" : "closed")
             << "</td><td>" << row.journal_records << "</td></tr>";
    }
    html << "</table></div><h2>Checks</h2><ul>";
    for (const auto& check : result.checks) {
        checks.push_back({{"name", check.name}, {"passed", check.passed}});
        html << "<li>" << check.name << ": " << (check.passed ? "passed" : "failed") << "</li>";
    }
    html << "</ul><p>Preserved incomplete tail: " << result.preserved_tail_bytes
         << " bytes.</p><h2>Reproduction evidence</h2><p>Source SHA-256: <code>" << build::source_sha256
         << "</code></p><p><a href=\"recovery_checks.json\">Checks and checkpoints</a> | <a href=\"summary.json\">Manifest</a> | "
            "<a href=\"source_inventory.txt\">Source inventory</a></p><p>The routine contract suite separately launches four "
            "abrupt-exit child processes. This report exercises closed/reopened files and constructed damaged files. "
            "No broker transport, automatic resend or power-loss hardware test is performed.</p></main></html>\n";
    write_new_text_file(directory / "recovery_checks.json", Json{{"checks", checks}, {"checkpoints", checkpoints}}.dump(2) + '\n');
    write_new_text_file(directory / "report.html", html.str());
    write_new_text_file(directory / "source_inventory.txt", build::source_records);
    auto files = result.journal_files;
    for (const auto* file : {"recovery_checks.json", "report.html", "source_inventory.txt"}) { files.emplace_back(file); }
    Json artifacts = Json::array();
    for (const auto& file : files) { artifacts.push_back({{"path", file}, {"sha256", sha256_file(directory / file)}}); }
    Json summary{{"schema_version", 1}, {"status", "synthetic_recovery_contracts_passed"},
        {"checked_assertions", checks.size()}, {"state_checkpoints", checkpoints.size()},
        {"preserved_tail_bytes", result.preserved_tail_bytes}, {"model_contract", "whole_share_limit_v1"},
        {"source_sha256", build::source_sha256}, {"compiler", TRADING_COMPILER_ID},
        {"build_configuration", TRADING_BUILD_CONFIGURATION}, {"artifacts", artifacts}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}

} // namespace trading::execution
