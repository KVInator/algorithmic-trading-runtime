#include "paper_execution_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <sstream>

namespace trading::execution {
namespace {
std::string dollars(std::int64_t units) {
    auto text = std::to_string(units);
    const bool negative = text.front() == '-';
    if (negative)
        text.erase(0, 1);
    if (text.size() <= 9)
        text.insert(0, 10 - text.size(), '0');
    text.insert(text.size() - 9, 1, '.');
    if (negative)
        text.insert(text.begin(), '-');
    return BrokerDecimal{text}.text();
}
} // namespace

void PaperExecutionReport::write(const std::filesystem::path& directory) const {
    using Json = nlohmann::json;
    Json artifacts = Json::array();
    const auto save = [&](std::string_view file, const std::string& bytes) {
        write_new_text_file(directory / file, bytes);
        artifacts.push_back({{"path", file}, {"sha256", sha256_file(directory / file)}});
    };
    for (const auto name : {"continuous.journal", "restarted.journal", "quarantine.journal"})
        artifacts.push_back({{"path", name}, {"sha256", sha256_file(directory / name)}});
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>Paper execution accounting bridge</title>"
            "<style>body{font:16px system-ui;max-width:1200px;margin:2rem auto;padding:0 "
            "1rem;color:#1b3047;background:#f3f7fa}aside{background:#fff0d1;border-left:4px solid "
            "#bb8618;padding:1rem}table{width:100%;border-collapse:collapse;background:white}th,td{text-"
            "align:left;padding:10px;border-bottom:1px solid "
            "#cbd8e5}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}.scroll{overflow:auto}"
            "details{background:white;padding:1rem;margin:1rem 0}summary{cursor:pointer}</style>"
            "<h1>Paper execution: exact accounting and durable identities</h1><aside>Constructed messages "
            "only. No network requests or broker-observed events. "
            "The declared paper_zero_fees_v1 convention books zero fees; it is not evidence that fees were "
            "observed. "
            "Schema 3 uses USD 1e-9 integer units and whole executed shares. Rounded cash snapshots cannot "
            "overwrite exact balances.</aside>"
            "<p>Each input and resulting checkpoint is synchronized before publication. Restart restores "
            "identities, executions and reservations with routing closed. "
            "The paired paths have identical final financial and identity state; their journals retain "
            "different restart histories.</p>"
            "<div class=\"scroll\"><table><tr><th>Case / event</th><th>Cash USD</th><th>Reserved "
            "USD</th><th>Shares</th><th>Sell reserve</th><th>Routing</th></tr>";
    for (const auto& frame : result_.frames) {
        const auto& s = frame.at("state");
        const auto& a = s.at("account");
        const bool open = a.at("connected").get<bool>() && !a.at("reconciliation_required").get<bool>() &&
                          !s.at("unresolved_evidence").get<bool>();
        html << "<tr><td>"
             << ReportText{frame.at("case").get<std::string>() + " / " + frame.at("label").get<std::string>()}
                    .html()
             << "</td><td>" << dollars(a.at("cash_units").get<std::int64_t>()) << "</td><td>"
             << dollars(a.at("reserved_cash_units").get<std::int64_t>()) << "</td><td>"
             << a.at("position_shares") << "</td><td>" << a.at("reserved_sell_shares") << "</td><td>"
             << (open ? "OPEN" : "CLOSED") << "</td></tr>";
    }
    html << "</table></div><h2>Assertions</h2><ul>";
    for (const auto& check : result_.checks)
        html << "<li>" << ReportText{check}.html() << "</li>";
    html << "</ul><h2>Inspect saved state</h2>";
    for (const auto& f : result_.frames)
        html << "<details><summary>"
             << ReportText{f.at("case").get<std::string>() + " / " + f.at("label").get<std::string>()}.html()
             << "</summary><pre>" << ReportText{f.at("state").dump(2)}.html() << "</pre></details>";
    html << "<p><a href=\"bridge_evidence.json\">Saved state and checks</a> · <a "
            "href=\"continuous.journal\">Continuous journal</a> · <a href=\"restarted.journal\">Restarted "
            "journal</a> · <a href=\"quarantine.journal\">Quarantined input</a> · <a "
            "href=\"source_inventory.txt\">Source inventory</a> · <a "
            "href=\"summary.json\">Manifest</a></p></html>\n";
    save("bridge_evidence.json", Json{{"checks", result_.checks}, {"frames", result_.frames}}.dump(2) + '\n');
    save("source_inventory.txt", std::string{build::source_records});
    save("report.html", html.str());
    write_new_text_file(directory / "summary.json", Json{{"schema_version", 3},
                                                         {"status", "constructed_paper_bridge_checks_passed"},
                                                         {"checked_assertions", result_.checks.size()},
                                                         {"case_count", 3},
                                                         {"network_requests_sent", 0},
                                                         {"broker_events_observed", 0},
                                                         {"money_unit", "USD_1e-9"},
                                                         {"fee_policy", "paper_zero_fees_v1"},
                                                         {"source_sha256", build::source_sha256},
                                                         {"compiler", TRADING_COMPILER_ID},
                                                         {"build_configuration", TRADING_BUILD_CONFIGURATION},
                                                         {"artifacts", artifacts}}
                                                            .dump(2) +
                                                        '\n');
}
} // namespace trading::execution
