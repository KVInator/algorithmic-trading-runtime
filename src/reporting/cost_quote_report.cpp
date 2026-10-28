#include "cost_quote_report.hpp"
#include "artifact_io.hpp"
#include "json_input.hpp"
#include "report_text.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string text(const std::string &value) { return ReportText{value}.html(); }
std::string number(double value, int places = 3) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(places) << value;
    return out.str();
}
std::string color(const std::string &status) {
    if (status == "eligible") {
        return "#177da3";
    }
    if (status == "nonregular") {
        return "#b47a28";
    }
    if (status == "ambiguous_latest") {
        return "#8863a3";
    }
    if (status == "empty") {
        return "#95a3ad";
    }
    return "#bb6251";
}
struct QuoteGroup {
    std::string symbol, year;
    std::vector<const Json *> rows;
    std::size_t eligible{};
    [[nodiscard]] std::string key() const { return symbol + '-' + year; }
};

// All numerical views are rendered once from saved records. JavaScript only
// selects an already-rendered group; it does not calculate a second chart.
class QuoteReportView {
  public:
    explicit QuoteReportView(const Json &summary) : summary_(summary) {
        if (summary.at("kind") != "cost_quote_sample" ||
            summary.at("policy_sha256").get<std::string>() != TRADING_COST_EXECUTION_PROTOCOL_SHA256 ||
            summary.at("windows").empty() || summary.at("windows").size() > 910 ||
            summary.at("plan").at("sample_dates").empty() ||
            summary.at("plan").at("sample_dates").size() > 7 ||
            summary.at("plan").at("symbols").size() != 5) {
            throw std::invalid_argument("quote report requires a bounded registered sample summary");
        }
        for (const auto &date : summary.at("plan").at("sample_dates")) {
            years_.push_back(date.get<std::string>().substr(0, 4));
        }
        for (const auto &symbol : summary.at("plan").at("symbols")) {
            for (const auto &year : years_) {
                QuoteGroup group{symbol.get<std::string>(), year, {}, 0};
                for (const auto &row : summary.at("windows")) {
                    if (row.at("symbol") == group.symbol &&
                        row.at("date").get<std::string>().starts_with(year)) {
                        group.rows.push_back(&row);
                        const auto status = row.at("status").get<std::string>();
                        ++counts_[status];
                        if (status == "eligible") {
                            const double value = row.at("measurement").at("half_spread_bps").get<double>();
                            if (!std::isfinite(value) || value < 0) {
                                throw std::invalid_argument("quote report contains an invalid spread");
                            }
                            ++group.eligible;
                            ++eligible_;
                        }
                    }
                }
                if (group.rows.empty()) {
                    throw std::invalid_argument("quote report has an empty planned group");
                }
                groups_.push_back(std::move(group));
            }
        }
        if (Json(counts_) != summary.at("status_counts")) {
            throw std::invalid_argument("quote report coverage differs from its saved windows");
        }
        const auto first =
            std::find_if(groups_.begin(), groups_.end(), [](const auto &g) { return g.eligible > 0; });
        initial_ = first == groups_.end() ? 0 : static_cast<std::size_t>(first - groups_.begin());
    }
    [[nodiscard]] std::string html() const {
        std::ostringstream out;
        out << R"html(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="quote-report-version" content="2"><meta name="report-renderer-source-sha256" content=")html"
            << TRADING_SOURCE_SHA256 << R"html("><title>Historical quote coverage and spreads</title><style>
body{font:16px/1.5 system-ui,sans-serif;background:#eef3f7;color:#17354b;margin:0}main{max-width:1150px;margin:auto;padding:28px 20px}h1{line-height:1.2}h2{font-size:22px}section,.sample-detail{background:white;border:1px solid #d5e0e7;border-radius:8px;padding:20px;margin:18px 0}.sample-detail{border:0;padding:0}svg{display:block;width:100%;height:auto;font:15px system-ui,sans-serif}select{font:inherit;padding:8px;margin-right:16px}.metrics{display:flex;flex-wrap:wrap;gap:14px}.metric{flex:1;min-width:155px;background:white;border:1px solid #d5e0e7;border-radius:8px;padding:15px}.metric strong{display:block;font-size:28px}.muted{color:#546b7b}.scroll{overflow-x:auto}table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:right;padding:7px;border-bottom:1px solid #dce5ec;white-space:nowrap}th:first-child,td:first-child{text-align:left}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}a{color:#126a8b}[hidden]{display:none!important}.notice{background:#f3f6f8;border-left:4px solid #95a3ad;padding:12px}summary{cursor:pointer;font-weight:600}@media(max-width:600px){main{padding:16px 10px}section{padding:12px}.metric strong{font-size:24px}}
</style></head><body><main><h1>Historical quote coverage and spreads</h1><p>)html"
            << (summary_.at("evidence_origin") == "synthetic"
                    ? "Constructed diagnostic. No historical market quotes were loaded."
                    : "Historical SIP sample. Observed quotes with hypothetical execution interpretation.")
            << "</p><p>Fixed old windows use the first common operational date per year. These are sampled "
               "quotes, not observed fills or complete strategy implementation costs.</p><div "
               "class=\"metrics\">"
            << "<div class=\"metric\">Planned windows<strong>" << summary_.at("windows").size()
            << "</strong></div>"
            << "<div class=\"metric\">Eligible<strong>" << eligible_ << "</strong>"
            << number(100.0 * static_cast<double>(eligible_) /
                          static_cast<double>(summary_.at("windows").size()),
                      1)
            << "% of planned windows</div><div class=\"metric\">Unestimated<strong>"
            << summary_.at("windows").size() - eligible_ << "</strong></div>"
            << "<div class=\"metric\">Retained HTTP attempts<strong>" << summary_.at("retained_http_attempts")
            << "</strong></div></div><section><h2>Eligibility by instrument and sampled year</h2>"
            << "<p>Each cell shows eligible / planned windows. Gray cells contain no eligible observations; "
               "they do not imply zero spreads. Every planned window remains in the denominator.</p>"
            << coverage_svg() << "</section><section><h2>Window dispositions</h2>" << status_svg()
            << "<p><code>nonregular</code> means the saved latest quote failed the registered condition-flag "
               "rule. It does not by itself establish abnormal market trading. Conflicting latest records "
               "remain ambiguous. Empty, locked or otherwise ineligible windows remain "
               "unestimated.</p></section>"
            << "<section><h2>Sampled half-spread detail</h2><p>The initial view is the first instrument/year "
               "with eligible records, using plan order. It is a display choice, not a replacement sample or "
               "policy change.</p><label>Instrument <select id=\"symbol\" disabled>";
        for (const auto &symbol : summary_.at("plan").at("symbols")) {
            const auto value = symbol.get<std::string>();
            out << "<option value=\"" << text(value) << '"'
                << (value == groups_[initial_].symbol ? " selected" : "") << '>' << text(value)
                << "</option>";
        }
        out << "</select></label><label>Year <select id=\"year\" disabled>";
        for (const auto &year : years_) {
            out << "<option value=\"" << text(year) << '"'
                << (year == groups_[initial_].year ? " selected" : "") << '>' << text(year) << "</option>";
        }
        out << "</select></label><noscript><p class=\"notice\">JavaScript is disabled. Overview plots and "
               "the initial detail remain visible; changing the instrument/year requires a browser with "
               "scripts enabled.</p></noscript>"
            << "<p class=\"muted\">Blue: one-minute delay. Orange: two-minute delay. Smaller orange marks "
               "preserve coincident blue observations. Half-spread replaces an all-in crossing charge; it is "
               "not added to it.</p>";
        for (std::size_t i = 0; i < groups_.size(); ++i) {
            detail(out, groups_[i], i != initial_);
        }
        out << "</section><section><details><summary>Acquisition identity and scope</summary><pre>"
            << text(Json{{"status", summary_.at("status")},
                         {"counts", summary_.at("status_counts")},
                         {"policy_sha256", summary_.at("policy_sha256")},
                         {"producer_source_sha256", summary_.at("source_sha256")},
                         {"report_renderer_source_sha256", TRADING_SOURCE_SHA256},
                         {"historical_strategy_scored", summary_.at("historical_strategy_scored")},
                         {"reserved_strategy_inputs_opened", summary_.at("reserved_strategy_inputs_opened")}}
                        .dump(2))
            << "</pre></details><p><a href=\"summary.json\">Original saved manifest</a>. Source timestamps "
               "retain nanoseconds; retrieval time is not historical receipt latency. Displayed sizes do not "
               "establish executable fills.</p></section>"
            << "<script>const data=" << summary_.dump() << R"html(;
const symbol=document.getElementById('symbol'),year=document.getElementById('year');
const panels=[...document.querySelectorAll('.sample-detail')];
function draw(){const key=symbol.value+'-'+year.value;for(const panel of panels){panel.hidden=panel.dataset.key!==key;}}
symbol.disabled=false;year.disabled=false;symbol.addEventListener('change',draw);year.addEventListener('change',draw);draw();
</script></main></body></html>)html";
        return out.str();
    }

  private:
    [[nodiscard]] std::string coverage_svg() const {
        std::ostringstream out;
        const double width = 830.0 / static_cast<double>(years_.size());
        out << "<svg id=\"coverage-chart\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 335\" "
               "role=\"img\" aria-label=\"Eligible quote windows by instrument and year\"><title>Eligible / "
               "planned quote windows</title>";
        for (std::size_t i = 0; i < years_.size(); ++i) {
            out << "<text x=\"" << number(120 + (static_cast<double>(i) + .5) * width)
                << "\" y=\"27\" text-anchor=\"middle\">" << text(years_[i]) << "</text>";
        }
        for (std::size_t i = 0; i < groups_.size(); ++i) {
            const auto &g = groups_[i];
            const auto column = i % years_.size(), row = i / years_.size();
            const double x = 120 + static_cast<double>(column) * width,
                         y = 45 + static_cast<double>(row) * 53;
            if (column == 0) {
                out << "<text x=\"10\" y=\"" << number(y + 29) << "\">" << text(g.symbol) << "</text>";
            }
            out << "<g data-symbol=\"" << text(g.symbol) << "\" data-year=\"" << text(g.year)
                << "\" data-total=\"" << g.rows.size() << "\" data-eligible=\"" << g.eligible
                << "\"><rect x=\"" << number(x) << "\" y=\"" << number(y) << "\" width=\""
                << number(width - 8) << "\" height=\"45\" rx=\"4\" fill=\""
                << (g.eligible ? "#d5ebf3" : "#edf0f3") << "\"/><text x=\"" << number(x + (width - 8) / 2)
                << "\" y=\"" << number(y + 29) << "\" text-anchor=\"middle\">" << g.eligible << " / "
                << g.rows.size() << "</text></g>";
        }
        return out.str() + "</svg>";
    }
    [[nodiscard]] std::string status_svg() const {
        std::ostringstream out;
        const double height = 65 + static_cast<double>(counts_.size()) * 30;
        out << "<svg id=\"status-chart\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 "
            << number(height)
            << "\" role=\"img\" aria-label=\"All sampled quote window dispositions\"><title>Window status "
               "counts</title>";
        double x = 10;
        std::size_t index = 0;
        for (const auto &[status, count] : counts_) {
            const double width =
                970 * static_cast<double>(count) / static_cast<double>(summary_.at("windows").size());
            out << "<rect data-status=\"" << text(status) << "\" data-count=\"" << count << "\" x=\""
                << number(x) << "\" y=\"10\" width=\"" << number(width) << "\" height=\"32\" fill=\""
                << color(status) << "\"><title>" << text(status) << ": " << count << "</title></rect>";
            const double y = 67 + static_cast<double>(index++) * 30;
            out << "<rect x=\"10\" y=\"" << number(y - 15) << "\" width=\"16\" height=\"16\" fill=\""
                << color(status) << "\"/><text x=\"38\" y=\"" << number(y) << "\">" << text(status) << ": "
                << count << " ("
                << number(100.0 * static_cast<double>(count) /
                              static_cast<double>(summary_.at("windows").size()),
                          1)
                << "%)</text>";
            x += width;
        }
        return out.str() + "</svg>";
    }
    void detail(std::ostream &out, const QuoteGroup &group, bool hidden) const {
        const auto key = text(group.key());
        out << "<article class=\"sample-detail\" data-key=\"" << key << '"' << (hidden ? " hidden" : "")
            << "><h3>" << text(group.symbol) << " / " << text(group.year) << "</h3><p>" << group.eligible
            << " / " << group.rows.size() << " eligible windows; unestimated "
            << group.rows.size() - group.eligible
            << ". Sample date: " << text(group.rows.front()->at("date").get<std::string>()) << ".</p>";
        double maximum = 0;
        for (const auto *row : group.rows) {
            if (row->at("status") == "eligible") {
                maximum = std::max(maximum, row->at("measurement").at("half_spread_bps").get<double>());
            }
        }
        const double limit = maximum > 0 ? maximum * 1.1 : 1;
        out << "<svg id=\"spread-" << key << "\" data-ymax=\"" << number(limit, 17)
            << "\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 310\" role=\"img\" aria-label=\""
            << text(group.symbol) << ' ' << text(group.year)
            << " sampled half-spread\"><title>Sampled half-spread in basis points</title>";
        if (group.eligible == 0) {
            out << "<rect width=\"1000\" height=\"310\" fill=\"#f3f6f8\"/><text x=\"500\" y=\"135\" "
                   "text-anchor=\"middle\">No eligible quotes in this selection</text><text x=\"500\" "
                   "y=\"170\" text-anchor=\"middle\">All "
                << group.rows.size() << " windows remain unestimated under the registered rule.</text>";
        } else {
            for (int tick = 0; tick <= 4; ++tick) {
                const double y = 245 - static_cast<double>(tick) * 205 / 4;
                out << "<line x1=\"100\" x2=\"920\" y1=\"" << number(y) << "\" y2=\"" << number(y)
                    << "\" stroke=\"#dce5ec\"/><text x=\"90\" y=\"" << number(y + 5)
                    << "\" text-anchor=\"end\">" << number(limit * static_cast<double>(tick) / 4, 4)
                    << "</text>";
            }
            out << "<text x=\"100\" y=\"22\">Half-spread, bp</text>";
            for (const auto *row : group.rows) {
                if (row->at("status") != "eligible") {
                    continue;
                }
                const double value = row->at("measurement").at("half_spread_bps").get<double>();
                const int delay = row->at("delay_minutes").get<int>();
                out << "<circle data-window-id=\"" << text(row->at("id").get<std::string>()) << "\" cx=\""
                    << number(100 +
                              static_cast<double>(row->at("decision_minute").get<int>() - 30) / 355 * 820)
                    << "\" cy=\"" << number(245 - value / limit * 205) << "\" r=\"" << (delay == 1 ? 6 : 3)
                    << "\" fill=\"" << (delay == 1 ? "#177da3" : "#b45c26") << "\"><title>"
                    << text(row->at("end").get<std::string>()) << ": " << number(value, 6) << " bp; delay "
                    << delay << " min</title></circle>";
            }
            for (int minute : {30, 120, 210, 300, 385}) {
                out << "<text x=\"" << number(100 + static_cast<double>(minute - 30) / 355 * 820)
                    << "\" y=\"272\" text-anchor=\"middle\">" << minute << "</text>";
            }
            out << "<text x=\"500\" y=\"302\" text-anchor=\"middle\">Decision offset from session open, "
                   "minutes</text>";
        }
        out << "</svg><details><summary>All selected windows and measurements</summary><div "
               "class=\"scroll\"><table><thead><tr><th>Reference UTC</th><th>Decision / delay, "
               "min</th><th>Status</th><th>Pages / records</th><th>Half-spread, bp</th><th>Bid / ask "
               "shares</th><th>Age, ms</th></tr></thead><tbody>";
        for (const auto *row : group.rows) {
            const auto &m = row->at("measurement");
            out << "<tr data-window-id=\"" << text(row->at("id").get<std::string>()) << "\"><td>"
                << text(row->at("end").get<std::string>()) << "</td><td>" << row->at("decision_minute")
                << " / " << row->at("delay_minutes") << "</td><td>"
                << text(row->at("status").get<std::string>()) << "</td><td>" << row->at("pages").size()
                << " / " << row->at("records") << "</td><td>"
                << (row->at("status") == "eligible" ? number(m.at("half_spread_bps").get<double>(), 6) : "NA")
                << "</td><td>";
            if (m.is_null()) {
                out << "NA</td><td>NA";
            } else {
                out << m.at("bid_shares") << " / " << m.at("ask_shares") << "</td><td>"
                    << number(m.at("age_nanoseconds").get<double>() / 1e6, 6);
            }
            out << "</td></tr>";
        }
        out << "</tbody></table></div></details></article>";
    }
    const Json &summary_;
    std::vector<std::string> years_;
    std::vector<QuoteGroup> groups_;
    std::map<std::string, std::size_t> counts_;
    std::size_t eligible_{}, initial_{};
};
void write_report(const std::filesystem::path &directory, const Json &summary, const std::string &saved) {
    const auto html = QuoteReportView{summary}.html();
    write_new_text_file(directory / "report.html", html);
    write_new_text_file(directory / "summary.json", saved);
}
} // namespace
void CostQuoteReport::write(const std::filesystem::path &directory, const Json &summary) const {
    write_report(directory, summary, summary.dump(2) + '\n');
}
void CostQuoteReport::render_existing(const std::filesystem::path &directory,
                                      const std::filesystem::path &saved_summary) const {
    const auto saved = read_text_file(saved_summary, 16U * 1024U * 1024U);
    write_report(directory, parse_json_document(saved), saved);
}
} // namespace trading
