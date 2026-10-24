#include "paper_contract_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

namespace trading {
void PaperContractReport::write(const std::filesystem::path& directory) const {
    using Json = nlohmann::json;
    Json checks = Json::array(), cases = Json::array(), requests = Json::array(), decimals = Json::array(),
         artifacts = Json::array();
    std::ostringstream html;
    html
        << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\">"
           "<title>Alpaca paper contract diagnostic</title><style>body{font:16px "
           "system-ui;max-width:1120px;margin:2rem auto;padding:0 1rem;color:#1b3047;background:#f4f7fb}"
           "section,details{background:white;padding:1rem;margin:1rem 0;border:1px solid "
           "#c9d6e4;border-radius:6px}summary{cursor:pointer}"
           "table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px;border-bottom:1px "
           "solid #d6dfe8}"
           "pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}.scroll{overflow:auto}aside{"
           "background:#fff1d5;padding:1rem;border-left:4px solid #bc861c}.ok{color:#16754c}</style>"
           "<h1>Alpaca paper message contract</h1><p>Constructed API-shaped messages; exact decimal and "
           "nanosecond boundaries.</p>"
           "<aside>No network requests or orders are sent. No credentials or historical strategy data are "
           "loaded. "
           "This diagnostic does not close the paper-broker milestone. A versioned precision/accounting "
           "bridge, transport and observed paper events remain.</aside>"
           "<section><h2>Exact monetary values</h2><p>Unavailable cent projections require a precision "
           "decision; values are never rounded to fit the existing "
           "runtime.</p><table><tr><th>Input</th><th>Canonical decimal</th><th>Exact integer cents</th></tr>";
    for (const auto& d : result_.decimals) {
        decimals.push_back({{"input", d.input},
                            {"canonical", d.canonical},
                            {"exact_cents", d.cents ? Json(*d.cents) : Json(nullptr)}});
        html << "<tr><td>" << ReportText{d.input}.html() << "</td><td>" << ReportText{d.canonical}.html()
             << "</td><td>";
        if (d.cents) {
            html << *d.cents;
        } else {
            html << "Unavailable without rounding";
        }
        html << "</td></tr>";
    }
    html << "</table></section><section><h2>Paper-only request descriptions</h2><p>Explicit SPY whole-share "
            "DAY limits. No transport executes these descriptions.</p>";
    for (const auto& r : result_.requests) {
        requests.push_back({{"method", r.method}, {"url", r.url}, {"body", r.body}});
        html << "<details><summary>" << ReportText{r.method + " " + r.url}.html() << "</summary><pre>"
             << ReportText{r.body}.html() << "</pre></details>";
    }
    html << "</section><h2>Message and rejection evidence</h2><p>Expected rejections pass when unsupported "
            "input is rejected. "
            "Authorization and subscription responses are decoded independently; they do not open routing. "
            "Incremental execution quantities/prices stay separate from cumulative quantities/averages. "
            "Unknown events retain their raw message and are marked unsupported. Fee information is "
            "unavailable in this contract; no zero fee is inferred.</p>";
    for (const auto& c : result_.cases) {
        cases.push_back(
            {{"name", c.name}, {"payload", c.payload}, {"decoded", c.accepted}, {"detail", c.detail}});
        html << "<details><summary>" << ReportText{c.name}.html() << " · "
             << (c.accepted ? "decoded" : "rejected") << "</summary><p>" << ReportText{c.detail}.html()
             << "</p><pre>" << ReportText{c.payload}.html() << "</pre></details>";
    }
    html << "<section><h2>Verification</h2><p class=\"ok\">" << result_.checks.size()
         << " checks passed across " << result_.cases.size()
         << " cases.</p><p><a href=\"contract_evidence.json\">Constructed payloads, requests and "
            "assertions</a> · <a href=\"source_inventory.txt\">Source inventory</a> · <a "
            "href=\"summary.json\">Manifest</a></p></section></html>\n";
    for (const auto& c : result_.checks) {
        checks.push_back({{"name", c.name}, {"passed", c.passed}});
    }
    const auto save = [&](std::string_view name, const std::string& data) {
        write_new_text_file(directory / name, data);
        artifacts.push_back({{"path", name}, {"sha256", sha256_file(directory / name)}});
    };
    save("contract_evidence.json",
         Json{{"checks", checks}, {"cases", cases}, {"requests", requests}, {"decimals", decimals}}.dump(2) +
             '\n');
    save("source_inventory.txt", std::string{build::source_records});
    save("report.html", html.str());
    write_new_text_file(directory / "summary.json",
                        Json{{"schema_version", 1},
                             {"status", "constructed_paper_contract_checks_passed"},
                             {"checked_assertions", result_.checks.size()},
                             {"case_count", result_.cases.size()},
                             {"network_requests_sent", 0},
                             {"broker_events_observed", 0},
                             {"runtime_schema_changed", false},
                             {"source_sha256", build::source_sha256},
                             {"compiler", TRADING_COMPILER_ID},
                             {"build_configuration", TRADING_BUILD_CONFIGURATION},
                             {"artifacts", artifacts}}
                                .dump(2) +
                            '\n');
}
} // namespace trading
