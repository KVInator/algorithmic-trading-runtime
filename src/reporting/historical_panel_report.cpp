#include "historical_panel_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include <map>
#include <sstream>

namespace trading {
std::filesystem::path HistoricalPanelReport::write(const nlohmann::json& summary, const std::filesystem::path& run_root) const {
    const auto directory = create_unique_directory(run_root, "historical_panel");
    std::ostringstream csv, html;
    csv << "symbol,date,regular,expected_minutes,observed_minutes,missing_minutes,open_available,close_available,intraday_eligible,common_intraday_eligible,reserved\n";
    struct Counts { std::size_t expected{}, observed{}, eligible{}, total{}; };
    std::map<std::string, Counts> counts;
    for (const auto& row : summary.at("coverage")) {
        const auto symbol = row.at("symbol").get<std::string>(), date = row.at("date").get<std::string>();
        auto& count = counts[symbol + ' ' + date.substr(0, 4)];
        count.expected += row.at("expected_minutes").get<std::size_t>();
        count.observed += row.at("observed_minutes").get<std::size_t>();
        count.eligible += row.at("intraday_eligible").get<bool>() ? 1U : 0U;
        ++count.total;
        csv << symbol << ',' << date;
        for (const auto* key : {"regular", "expected_minutes", "observed_minutes", "missing_minutes", "open_available",
                "close_available", "intraday_eligible", "common_intraday_eligible", "reserved"}) { csv << ',' << row.at(key); }
        csv << '\n';
    }
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        << "<title>Historical panel coverage</title><style>body{font:16px/1.5 system-ui;margin:32px;color:#16334a}"
        << "main{max-width:1100px;margin:auto}table{border-collapse:collapse;width:100%}th,td{padding:9px;text-align:left;border-bottom:1px solid #ccd5dc}</style>"
        << "<main><h1>Historical panel coverage</h1><p>Evidence: " << ReportText{summary.at("evidence_origin").get<std::string>()}.html()
        << ". Status: " << ReportText{summary.at("status").get<std::string>()}.html()
        << ". No strategy was fitted or scored. The table reports input coverage, not research results.</p>"
        << "<table><tr><th>Symbol / year</th><th>Calendar dates</th><th>Eligible full sessions</th><th>Observed / expected minutes</th></tr>";
    for (const auto& [key, value] : counts) {
        html << "<tr><td>" << ReportText{key}.html() << "</td><td>" << value.total << "</td><td>" << value.eligible
            << "</td><td>" << value.observed << " / " << value.expected << "</td></tr>";
    }
    html << "</table><p>Complete full-session inputs define primary intraday eligibility. Shortened sessions remain in daily-account coverage. "
        << "Missing bars remain missing. Common dates require all five symbols; inactive strategy decisions are a later calculation.</p>"
        << "<p>Corporate-action reconciliation, initialization, source-vintage comparison and final dataset registration must pass before scoring. "
        << "Reserved 2024-2025 inputs remain sealed; coverage checks do not authorize research access.</p>"
        << "<p><a href=\"coverage.csv\">Daily coverage CSV</a> | <a href=\"summary.json\">Evidence identities</a></p></main></html>";
    auto result = summary;
    result["review_artifacts"] = {{{"path", "coverage.csv"}, {"sha256", sha256(csv.str())}},
        {{"path", "report.html"}, {"sha256", sha256(html.str())}}};
    write_new_text_file(directory / "coverage.csv", csv.str());
    write_new_text_file(directory / "report.html", html.str());
    write_new_text_file(directory / "summary.json", result.dump(2) + '\n');
    return directory;
}
} // namespace trading
