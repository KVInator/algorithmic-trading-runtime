#include "paper_market_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <sstream>
#include <tuple>

namespace trading {
void PaperMarketReport::write(const std::filesystem::path& directory) const {
    using Json = nlohmann::json;
    const auto save = [&](std::string_view name, std::string_view body) {
        write_new_text_file(directory / name, body);
    };
    save("market_evidence.json",
         Json{{"checks", result_.checks}, {"cases", result_.cases}, {"detail", result_.detail}}.dump(2) +
             '\n');
    save("source_inventory.txt", build::source_records);
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\">"
            "<title>Paper quote and calendar evidence</title><style>body{font:16px "
            "system-ui;max-width:1100px;margin:2rem auto;padding:0 1rem;color:#203247;background:#f4f7fa}"
            "aside,details{padding:1rem;background:white;margin:1rem 0}aside{border-left:4px solid "
            "#ad8317}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}"
            "table{border-collapse:collapse;width:100%;background:white}td,th{padding:.6rem;border-bottom:"
            "1px solid #ccd5de;text-align:left}summary{cursor:pointer}</style>"
            "<h1>Paper quote and calendar evidence</h1><aside>"
         << (result_.native_probe ? "Native read-only SIP quote/calendar access."
                                  : "Constructed observations: zero network requests and zero broker events.")
         << " No orders or session enrollment. Quote usability applies only at the saved evaluation time; "
            "account/control, entry-window and dispatch gates remain separate.</aside><p>"
         << ReportText{result_.detail}.html() << "</p><h2>Checks</h2><ul>";
    for (const auto& check : result_.checks) {
        html << "<li>" << ReportText{check}.html() << "</li>";
    }
    html << "</ul>";
    const auto render = [&](const Json& observation) {
        if (observation.at("case").get<std::string>() == "current_calendar") {
            html << "<h2>Calendar coverage</h2><table><tr><th>Date</th><th>Open UTC</th><th>Close "
                    "UTC</th><th>Regular session</th></tr>";
            for (const auto& s : observation.at("sessions")) {
                html << "<tr><td>" << ReportText{s.at("date").get<std::string>()}.html() << "</td><td>"
                     << ReportText{s.at("open_utc").get<std::string>()}.html() << "</td><td>"
                     << ReportText{s.at("close_utc").get<std::string>()}.html() << "</td><td>"
                     << (s.at("regular").get<bool>() ? "yes" : "excluded") << "</td></tr>";
            }
            html << "</table><p>Candidate sessions are provisional; no dates are enrolled.</p>";
        } else if (observation.at("case").get<std::string>() == "latest_sip_quote") {
            html << "<h2>Quote and limit projection</h2><table><tr><th>Bid USD</th><th>Ask "
                    "USD</th><th>Bid/ask round lots</th><th>Usable</th></tr><tr><td>"
                 << ReportText{observation.at("bid").get<std::string>()}.html() << "</td><td>"
                 << ReportText{observation.at("ask").get<std::string>()}.html() << "</td><td>"
                 << observation.at("bid_size_round_lots") << " / " << observation.at("ask_size_round_lots")
                 << "</td><td>"
                 << (observation.at("quote_usable").get<bool>() ? "yes, at saved time" : "blocked")
                 << "</td></tr></table>";
            html << "<p>Source: " << ReportText{observation.at("source_at").get<std::string>()}.html()
                 << " | Received: "
                 << ReportText{observation.at("received").at("utc").get<std::string>()}.html()
                 << " | Source age at evaluation: " << observation.at("source_age_ns")
                 << " ns</p>"
                    "<table><tr><th>Policy</th><th>Reference</th><th>Projected integer cents</th></tr>";
            for (const auto& [label, reference, field] :
                 std::vector<std::tuple<std::string, std::string, std::string>>{
                     {"Passive buy", "bid, down", "passive_buy_cents"},
                     {"Marketable buy", "ask, up", "marketable_buy_cents"},
                     {"Marketable sell", "bid, down", "marketable_sell_cents"}}) {
                html << "<tr><td>" << label << "</td><td>" << reference << "</td><td>";
                if (observation.at(field).is_null()) {
                    html << "unavailable";
                } else {
                    html << observation.at(field);
                }
                html << "</td></tr>";
            }
            html << "</table>";
        }
        html << "<details><summary>" << ReportText{observation.at("case").get<std::string>()}.html()
             << "</summary><pre>" << ReportText{observation.dump(2)}.html() << "</pre></details>";
    };
    for (const auto& c : result_.cases) {
        if (c.contains("observations")) {
            html << "<h2>Constructed case: " << ReportText{c.at("case").get<std::string>()}.html() << "</h2>";
            for (const auto& observation : c.at("observations")) {
                render(observation);
            }
        } else {
            render(c);
        }
    }
    html << "<p>Source age uses provider UTC and local UTC; unsynchronized host clocks remain a limitation. "
            "Local monotonic time checks receipt aging/regression. "
            "SIP identity is the explicit request feed, not an independently authenticated field in the "
            "quote body. Displayed round lots are retained without converting to shares or estimating fill "
            "probability.</p>"
            "<p><a href=\"market_evidence.json\">Saved evidence</a> | <a href=\"summary.json\">Manifest</a> "
            "| <a href=\"source_inventory.txt\">Source inventory</a></p></html>\n";
    save("report.html", html.str());
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() != ".tmp") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    Json artifacts = Json::array();
    for (const auto& p : files) {
        artifacts.push_back(
            {{"path", std::filesystem::relative(p, directory).generic_string()}, {"sha256", sha256_file(p)}});
    }
    save("summary.json",
         Json{{"schema_version", 1},
              {"model_contract", "paper_quote_calendar_v1"},
              {"status", result_.access_verified ? (result_.native_probe ? "read_only_access_verified"
                                                                         : "constructed_checks_completed")
                                                 : "probe_stopped"},
              {"native_read_only_probe", result_.native_probe},
              {"service_access_observed", result_.native_probe && result_.access_verified},
              {"request_attempts", result_.request_attempts},
              {"network_requests_sent", result_.native_probe ? Json(nullptr) : Json(0)},
              {"order_submissions", 0},
              {"session_enrollment_created", false},
              {"checked_assertions", result_.checks.size()},
              {"case_count", result_.cases.size()},
              {"source_sha256", build::source_sha256},
              {"compiler", TRADING_COMPILER_ID},
              {"build_configuration", TRADING_BUILD_CONFIGURATION},
              {"artifacts", artifacts}}
                 .dump(2) +
             '\n');
}
} // namespace trading
