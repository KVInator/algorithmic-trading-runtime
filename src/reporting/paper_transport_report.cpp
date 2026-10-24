#include "paper_transport_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <sstream>

namespace trading {
void PaperTransportReport::write(const std::filesystem::path& directory) const {
    using Json = nlohmann::json;
    Json artifacts = Json::array();
    const auto save = [&](std::string_view name, const std::string& bytes) {
        write_new_text_file(directory / name, bytes);
    };
    save("transport_evidence.json",
         Json{{"checks", result_.checks}, {"cases", result_.cases}, {"detail", result_.detail}}.dump(2) +
             '\n');
    save("source_inventory.txt", std::string{build::source_records});
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta "
            "name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>Paper "
            "transport evidence</title>"
            "<style>body{font:16px system-ui;max-width:1150px;margin:2rem "
            "auto;padding:0 "
            "1rem;color:#203247;background:#f4f7fa}aside{padding:1rem;background:"
            "#fff0d1;border-left:4px "
            "solid #ad8317}details{background:white;padding:1rem;margin:1rem "
            "0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}"
            "summary{cursor:pointer}li{"
            "margin:.4rem 0}</style>"
            "<h1>Alpaca paper transport</h1><aside>"
         << (result_.native_probe ? "Native read-only probe: HTTP queries and stream "
                                    "authorization/subscription; no order "
                                    "submission or cancellation."
                                  : "Deterministic wire faults: zero network requests and zero "
                                    "broker-observed order events.")
         << " A working connection does not close the paper-broker milestone. "
            "Observed order lifecycles, "
            "expiry/stop policy and five-session capture remain.</aside>"
         << "<p>Status: " << (result_.successful ? "checks completed" : "stopped with unresolved evidence")
         << "</p><p>" << ReportText{result_.detail}.html() << "</p><h2>Checks</h2><ul>";
    for (const auto& check : result_.checks)
        html << "<li>" << ReportText{check}.html() << "</li>";
    html << "</ul><h2>Cases and final state</h2>";
    for (const auto& c : result_.cases)
        html << "<details><summary>" << ReportText{c.at("case").get<std::string>()}.html()
             << "</summary><pre>" << ReportText{c.dump(2)}.html() << "</pre></details>";
    html << "<p>Raw bodies are synchronized before decoding and referenced by "
            "chained capture indexes. "
            "Outbound credential/authentication messages are excluded. Capture "
            "and runtime journals have "
            "separate commit boundaries; captured input is not proof that "
            "accounting committed it.</p><p><a "
            "href=\"transport_evidence.json\">Case evidence</a> · <a "
            "href=\"summary.json\">Artifact "
            "manifest</a> · <a href=\"source_inventory.txt\">Source "
            "inventory</a></p></html>\n";
    save("report.html", html.str());
    std::vector<std::filesystem::path> files;
    for (const auto& item : std::filesystem::recursive_directory_iterator(directory))
        if (item.is_regular_file() && item.path().extension() != ".tmp")
            files.push_back(item.path());
    std::sort(files.begin(), files.end());
    for (const auto& path : files)
        artifacts.push_back({{"path", std::filesystem::relative(path, directory).generic_string()},
                             {"sha256", sha256_file(path)}});
    save("summary.json",
         Json{{"schema_version", 1},
              {"status", result_.successful ? "transport_checks_completed" : "transport_probe_stopped"},
              {"native_read_only_probe", result_.native_probe},
              {"checked_assertions", result_.checks.size()},
              {"case_count", result_.cases.size()},
              {"broker_order_events_observed", result_.native_probe ? Json(nullptr) : Json(0)},
              {"real_order_submissions", 0},
              {"network_requests_sent", result_.native_probe ? Json(nullptr) : Json(0)},
              {"source_sha256", build::source_sha256},
              {"compiler", TRADING_COMPILER_ID},
              {"build_configuration", TRADING_BUILD_CONFIGURATION},
              {"artifacts", artifacts}}
                 .dump(2) +
             '\n');
}
} // namespace trading
