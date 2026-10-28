#include "cost_quote_diagnostic_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include <algorithm>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string text(const std::string &value) { return ReportText{value}.html(); }
std::string field_label(const Json &field) {
    if (!field.at("present").get<bool>()) {
        return "missing field";
    }
    return field.contains("value")
               ? field.at("value").dump()
               : "oversized metadata; SHA-256 " + field.at("value_sha256").get<std::string>();
}
} // namespace
void CostQuoteDiagnosticReport::write(const std::filesystem::path &directory, const Json &summary) const {
    std::map<std::string, std::map<std::string, std::size_t>> years;
    for (const auto &row : summary.at("windows")) {
        ++years[row.at("date").get<std::string>().substr(0, 4)][row.at("primary_status").get<std::string>()];
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << R"html(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Cached quote condition and timestamp diagnosis</title><style>body{font:16px/1.5 system-ui,sans-serif;background:#eef3f7;color:#17354b;margin:0}main{max-width:1150px;margin:auto;padding:28px 20px}section{background:white;border:1px solid #d5e0e7;border-radius:8px;padding:20px;margin:18px 0}svg{display:block;width:100%;height:auto;font:15px system-ui,sans-serif}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:left;padding:8px;border-bottom:1px solid #dce5ec}td code{white-space:pre-wrap;overflow-wrap:anywhere}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}a{color:#126a8b}</style></head><body><main><h1>Cached quote condition and timestamp diagnosis</h1><p>Read-only replay of retained HTTP responses. Primary classifications are unchanged; no new quotes, strategy outcomes or reserved inputs were requested.</p><p>Evidence origin: )html"
        << text(summary.at("evidence_origin").get<std::string>())
        << R"html(.</p><section><h2>Primary replay and scope</h2><p>)html" << summary.at("windows").size()
        << " windows; " << summary.at("retained_http_attempts")
        << " retained HTTP attempts. Original primary replay: identical. Network requests: 0.</p><p>Removing "
           "only the condition test would count "
        << summary.at("condition_test_removal_only_count")
        << " windows. This is a diagnostic count, not an adopted eligibility rule or proof of executable "
           "prices. Empty or unusual metadata is not silently reinterpreted.</p>"
        << "<p>Complete-window latest records drive the condition frequencies below. A window can contribute "
           "to several metadata patterns; counts across those patterns are not disjoint window totals. "
           "Duplicate records contribute to record counts, not repeated window "
           "counts.</p></section><section><h2>Registered dispositions by sampled year</h2>"
        << "<p>Blue: eligible. Amber: condition rule. Purple: ambiguous latest timestamp. Gray: all other "
           "unestimated windows.</p>"
        << "<svg id=\"diagnostic-coverage\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 "
        << 55 + years.size() * 48
        << "\" role=\"img\" aria-label=\"Primary quote dispositions by sampled year\">";
    std::size_t index = 0;
    for (auto &[year, counts] : years) {
        std::size_t total = 0;
        for (const auto &[status, count] : counts) {
            static_cast<void>(status);
            total += count;
        }
        const auto eligible = counts["eligible"], nonregular = counts["nonregular"],
                   ambiguous = counts["ambiguous_latest"];
        const auto other = total - eligible - nonregular - ambiguous;
        const double y = 15 + static_cast<double>(index++) * 48;
        out << "<text x=\"10\" y=\"" << y + 22 << "\">" << text(year) << "</text>";
        double x = 100;
        for (const auto &[status, count, color] :
             std::vector<std::tuple<std::string, std::size_t, std::string>>{
                 {"eligible", eligible, "#177da3"},
                 {"nonregular", nonregular, "#b47a28"},
                 {"ambiguous_latest", ambiguous, "#8863a3"},
                 {"other", other, "#95a3ad"}}) {
            const double width = 830.0 * static_cast<double>(count) / static_cast<double>(total);
            out << "<rect data-year=\"" << text(year) << "\" data-status=\"" << status << "\" data-count=\""
                << count << "\" x=\"" << x << "\" y=\"" << y << "\" width=\"" << width
                << "\" height=\"30\" fill=\"" << color << "\"><title>" << text(year) << ' ' << status << ": "
                << count << " / " << total << "</title></rect>";
            x += width;
        }
        out << "<text x=\"945\" y=\"" << y + 22 << "\">" << total << "</text>";
    }
    out << "</svg></section><section><h2>Why latest timestamps are ambiguous</h2><p>These categories apply "
           "only to the original ambiguous windows. Equal prices or sizes do not resolve conflicting "
           "condition flags, establish message order or automatically qualify a quote.</p><svg "
           "id=\"diagnostic-conflicts\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 240\" "
           "role=\"img\" aria-label=\"Latest timestamp conflict categories\">";
    const auto &relations = summary.at("ambiguous_latest_relations");
    std::size_t maximum = 1;
    for (const auto &[name, count] : relations.items()) {
        static_cast<void>(name);
        maximum = std::max(maximum, count.get<std::size_t>());
    }
    index = 0;
    for (const auto *label : {"price_conflict", "size_conflict", "metadata_only", "unsupported_fields"}) {
        const auto count = relations.contains(label) ? relations.at(label).get<std::size_t>() : 0;
        const double y = 15 + static_cast<double>(index++) * 52;
        out << "<text x=\"10\" y=\"" << y + 22 << "\">" << label << "</text><rect data-relation=\"" << label
            << "\" data-count=\"" << count << "\" x=\"220\" y=\"" << y << "\" width=\""
            << 650.0 * static_cast<double>(count) / static_cast<double>(maximum)
            << "\" height=\"32\" fill=\"#8863a3\"/><text x=\"900\" y=\"" << y + 22 << "\">" << count
            << "</text>";
    }
    out << "</svg></section><section><h2>Observed latest condition arrays and tapes</h2><p>Arrays are shown "
           "exactly, including empty arrays and blank strings. Missing fields are separate. Metadata "
           "representations over 512 bytes are identified by type and digest, with original bytes retained "
           "in the local response archive. Code meanings require separate tape/source documentation.</p><div "
           "class=\"scroll\"><table><thead><tr><th>Year</th><th>Instrument</th><th>Tape</th><th>Condition "
           "field</th><th>Windows</th><th>Latest records</th></tr></thead><tbody>";
    index = 0;
    for (const auto &row : summary.at("metadata_frequencies")) {
        out << "<tr data-frequency-index=\"" << index++ << "\"><td>"
            << text(row.at("year").get<std::string>()) << "</td><td>"
            << text(row.at("symbol").get<std::string>()) << "</td><td><code>"
            << text(field_label(row.at("tape"))) << "</code></td><td><code>"
            << text(field_label(row.at("conditions"))) << "</code></td><td>" << row.at("windows")
            << "</td><td>" << row.at("records") << "</td></tr>";
    }
    out << "</tbody></table></div></section><section><h2>Evidence identity</h2><p><a "
           "href=\"summary.json\">Complete diagnostic manifest</a>. Raw prices and response bodies stay in "
           "the original local archive.</p><pre>"
        << text(Json{{"source_summary_sha256", summary.at("source_summary_sha256")},
                     {"producer_source_sha256", summary.at("producer_source_sha256")},
                     {"diagnostic_source_sha256", summary.at("source_sha256")},
                     {"policy_sha256", summary.at("policy_sha256")},
                     {"revised_eligibility_adopted", false}}
                    .dump(2))
        << "</pre></section></main></body></html>";
    write_new_text_file(directory / "report.html", out.str());
    auto saved = summary;
    saved["report_sha256"] = sha256(out.str());
    write_new_text_file(directory / "summary.json", saved.dump(2) + '\n');
}
} // namespace trading
