#include "historical_capability_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include <sstream>

namespace trading {
void HistoricalCapabilityReport::write(const std::filesystem::path& directory, nlohmann::json summary) const {
    std::ostringstream csv, html;
    csv << "symbol,year,sampled_bars_complete,quotes,trades,actions,cash_dividends,complete_payment_fields,splits,complete_split_fields\n";
    html << R"(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Historical data capability</title><style>body{font:16px/1.5 system-ui;color:#173046;background:#eef3f6;margin:0}main{max-width:1200px;margin:auto;padding:32px}section{background:white;padding:20px;margin:18px 0;overflow:auto}table{border-collapse:collapse;width:100%}th,td{padding:8px;border-bottom:1px solid #ccd7df;text-align:left}.yes{background:#d9eee3}.no{background:#f5e1d9}h1{line-height:1.2}a{color:#16628a}</style><main>
<h1>Historical data capability</h1><p>Fixed five-instrument historical samples. No strategy fitting, return, spread or price summary is calculated.</p>)";
    html << "<section><b>Evidence: " << ReportText{summary.at("evidence_origin").get<std::string>()}.html()
         << "</b><p>Status: " << ReportText{summary.at("status").get<std::string>()}.html()
         << "</p><p>HTTP attempts: " << summary.at("request_attempts")
         << ". Full historical coverage and corporate-action reconciliation remain unverified.</p>"
         << "<p>Provisional common price start: " << summary.at("provisional_common_price_start")
         << ". This is based on two sampled full sessions per symbol/year, not an audit of every intervening date. "
         << "Initialization and scored-year counts require M2 verification.</p></section>"
         << "<section><h2>Capability matrix</h2><table><tr><th>Symbol</th><th>Year</th><th>Bar samples</th>"
         << "<th>Quotes</th><th>Trades</th><th>Actions</th><th>Payment fields</th><th>Split fields</th></tr>";
    for (const auto& row : summary.at("rows")) {
        const auto symbol = row.at("symbol").get<std::string>();
        const auto q = row.at("quotes").at("status").get<std::string>();
        const auto t = row.at("trades").at("status").get<std::string>();
        const auto& a = row.at("actions");
        const auto action = a.at("status").get<std::string>();
        const bool complete = row.at("sampled_bars_complete").get<bool>();
        csv << ReportText{symbol}.csv() << ',' << row.at("year") << ',' << (complete ? 1 : 0) << ','
            << ReportText{q}.csv() << ',' << ReportText{t}.csv() << ',' << ReportText{action}.csv() << ','
            << a.at("cash_dividends") << ',' << a.at("complete_payment_fields") << ','
            << a.at("splits") << ',' << a.at("complete_split_fields") << '\n';
        html << "<tr><td>" << ReportText{symbol}.html() << "</td><td>" << row.at("year")
             << "</td><td class=\"" << (complete ? "yes" : "no") << "\">" << (complete ? "Complete" : "Review")
             << "</td><td>" << ReportText{q}.html() << "</td><td>" << ReportText{t}.html()
             << "</td><td>" << ReportText{action}.html() << "</td><td>" << a.at("complete_payment_fields")
             << '/' << a.at("cash_dividends") << "</td><td>" << a.at("complete_split_fields") << '/' << a.at("splits")
             << "</td></tr>";
    }
    html << "</table></section><section><h2>Interpretation</h2><p>January's first and December's last full sessions "
         << "are selected from the returned calendar before bar inspection. Missing bars remain missing. Quotes/trades "
         << "use the first page, at most 100 records, in one fixed noon minute; a continuation token means the window "
         << "was not exhausted. Empty data do not prove that a market had no observations.</p>"
         << "<p>Actions are paginated by process date. Valid payment/split fields demonstrate schema availability, "
         << "not complete ex-date coverage, point-in-time announcement availability or issuer agreement. Zero actions "
         << "do not prove that no dividend or split occurred. Other action types remain visible in summary metadata.</p>"
         << "<p>HTTP 401 stops further requests. HTTP 400/403/404 block that endpoint/symbol for this run. "
         << "429 and transient 500/502/503/504 receive at most two retries; every received response is retained. "
         << "Repeated endpoint failure is retained without new probes. No paid access or feed substitution is attempted.</p>"
         << "<p>Share only the review directory or the independently verified review.zip. Raw responses and sealed "
         << "2024-2025 inputs remain local. The review bundle contains flags, counts, request metadata and hashes.</p>"
         << "<p><a href=\"capability.csv\">Capability CSV</a> | <a href=\"summary.json\">Summary and evidence identities</a></p>"
         << "</section></main></html>";
    const auto table = csv.str(), report = html.str();
    summary["review_artifacts"] = {{{"path", "capability.csv"}, {"sha256", sha256(table)}},
                                   {{"path", "report.html"}, {"sha256", sha256(report)}}};
    const auto text = summary.dump(2) + '\n';
    for (const auto& target : {directory, directory / "review"}) {
        write_new_text_file(target / "capability.csv", table);
        write_new_text_file(target / "report.html", report);
        write_new_text_file(target / "summary.json", text);
    }
}
} // namespace trading
