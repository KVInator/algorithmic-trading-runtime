#include "paper_transport_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace trading {
void PaperTransportReport::write(const std::filesystem::path &directory) const {
    using Json = nlohmann::json;
    Json artifacts = Json::array();
    const bool matched_broker = !result_.cases.empty() && result_.cases.at(0).contains("frames");
    const auto save = [&](std::string_view name, const std::string &bytes) {
        write_new_text_file(directory / name, bytes);
    };
    save("transport_evidence.json",
         Json{{"checks", result_.checks}, {"cases", result_.cases}, {"detail", result_.detail}}.dump(2) +
             '\n');
    save("source_inventory.txt", std::string{build::source_records});
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta "
            "name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>Simulated "
            "paper broker</title>"
            "<style>body{font:16px system-ui;max-width:1150px;margin:2rem "
            "auto;padding:0 "
            "1rem;color:#203247;background:#f4f7fa}aside{padding:1rem;background:"
            "#fff0d1;border-left:4px "
            "solid #ad8317}details{background:white;padding:1rem;margin:1rem "
            "0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}"
            "table{width:100%;border-collapse:collapse;background:white}th,td{padding:.6rem;border-bottom:"
            "1px solid #ccd6df;text-align:left}svg{width:100%;background:white}summary{cursor:pointer}li{"
            "margin:.4rem 0}</style>"
            "<h1>Simulated paper broker and recovery</h1><aside>"
         << "Synthetic protocol peer: all quotes, acknowledgments, fills, cancellations and delivery times "
            "are constructed. "
            "Zero network requests; no observed Alpaca executions. "
         << (matched_broker ? "Matching uses the shared FIFO top-of-book simulator."
                            : "Messages follow scripted protocol fault schedules.")
         << "</aside>"
         << "<p>Status: " << (result_.successful ? "checks completed" : "stopped with unresolved evidence")
         << "</p><p>" << ReportText{result_.detail}.html() << "</p><h2>Checks</h2><ul>";
    for (const auto &check : result_.checks)
        html << "<li>" << ReportText{check}.html() << "</li>";
    html << "</ul><h2>Cases and final state</h2>";
    for (const auto &c : result_.cases) {
        html << "<section><h3>" << ReportText{c.at("case").get<std::string>()}.html() << "</h3>";
        if (c.contains("frames")) {
            html << "<p>Event chronology. Cash is USD; quantity is whole shares. Expand a row for the saved "
                    "client and broker state.</p>";
            const auto &frames = c.at("frames");
            html << "<svg viewBox=\"0 0 800 125\" role=\"img\" aria-label=\"Position in shares at saved "
                    "event checkpoints\">"
                    "<text x=\"10\" y=\"15\">Position (shares), saved event checkpoints</text><path d=\"M20 "
                    "105H780\" stroke=\"#889aaa\"/>"
                    "<polyline fill=\"none\" stroke=\"#147d92\" stroke-width=\"3\" points=\"";
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const auto qty =
                    frames.at(i).at("state").at("account").at("position_shares").get<std::int64_t>();
                html << 20 + i * 740 / std::max<std::size_t>(1, frames.size() - 1) << ',' << 105 - qty * 4
                     << ' ';
            }
            html << "\"/>";
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const auto qty =
                    frames.at(i).at("state").at("account").at("position_shares").get<std::int64_t>();
                html << "<text font-size=\"11\" x=\""
                     << 20 + i * 740 / std::max<std::size_t>(1, frames.size() - 1) << "\" y=\""
                     << 95 - qty * 4 << "\">" << qty << "</text>";
            }
            html << "</svg><table><thead><tr><th>Event</th><th>Cash USD</th><th>Reserved "
                    "USD</th><th>Shares</th><th>Routing</th></tr></thead><tbody>";
            for (const auto &frame : frames) {
                const auto &account = frame.at("state").at("account");
                const bool ready =
                    account.at("connected").get<bool>() && !account.at("reconciliation_required").get<bool>();
                html << "<tr><td><details><summary>"
                     << ReportText{frame.at("event").get<std::string>()}.html() << "</summary><pre>"
                     << ReportText{frame.dump(2)}.html() << "</pre></details></td><td>" << std::fixed
                     << std::setprecision(2) << account.at("cash_units").get<double>() / 1e9 << "</td><td>"
                     << account.at("reserved_cash_units").get<double>() / 1e9 << "</td><td>"
                     << account.at("position_shares").get<std::int64_t>() << "</td><td>"
                     << (ready ? "open" : "closed") << "</td></tr>";
            }
            html << "</tbody></table>";
        }
        html << "<details><summary>Complete saved evidence</summary><pre>" << ReportText{c.dump(2)}.html()
             << "</pre></details></section>";
    }
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
    for (const auto &item : std::filesystem::recursive_directory_iterator(directory))
        if (item.is_regular_file() && item.path().extension() != ".tmp")
            files.push_back(item.path());
    std::sort(files.begin(), files.end());
    for (const auto &path : files)
        artifacts.push_back({{"path", std::filesystem::relative(path, directory).generic_string()},
                             {"sha256", sha256_file(path)}});
    save("summary.json",
         Json{{"schema_version", 1},
              {"status", result_.successful ? "transport_checks_completed" : "transport_probe_stopped"},
              {"native_read_only_probe", false},
              {"checked_assertions", result_.checks.size()},
              {"case_count", result_.cases.size()},
              {"broker_order_events_observed", 0},
              {"real_order_submissions", 0},
              {"network_requests_sent", 0},
              {"evidence_origin", "synthetic"},
              {"source_sha256", build::source_sha256},
              {"compiler", TRADING_COMPILER_ID},
              {"build_configuration", TRADING_BUILD_CONFIGURATION},
              {"artifacts", artifacts}}
                 .dump(2) +
             '\n');
}
} // namespace trading
