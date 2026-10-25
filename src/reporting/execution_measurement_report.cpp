#include "execution_measurement_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string shown(const Json &value, int precision = 3) {
    if (value.is_null())
        return "unavailable";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value.get<double>();
    return out.str();
}
class MeasurementFigure {
  public:
    explicit MeasurementFigure(const Json &c) : case_{c} {}
    std::string svg() const {
        CentsRange range;
        for (const auto &q : case_.at("quotes")) {
            range.low = std::min(range.low, q.at("bid_cents").get<double>());
            range.high = std::max(range.high, q.at("ask_cents").get<double>());
        }
        range.low = std::min(range.low, case_.at("limit_cents").get<double>()) - 1;
        range.high = std::max(range.high, case_.at("limit_cents").get<double>()) + 1;
        const auto x = [](double t) { return 60.0 + (t - 10.0) * 34.0; };
        const auto y = [&range](double p) {
            return 235.0 - (p - range.low) * 185.0 / (range.high - range.low);
        };
        std::ostringstream out;
        out << std::fixed << std::setprecision(2);
        out << "<svg viewBox=\"0 0 820 290\" role=\"img\" aria-label=\"Synthetic bid ask limit and "
               "source-time fills\">"
               "<rect width=\"820\" height=\"290\" fill=\"#fff\"/><text x=\"60\" y=\"20\">Bid / ask / fixed "
               "limit / fills</text>"
               "<text x=\"60\" y=\"37\" font-size=\"11\">Synthetic USD prices; circles show broker fill time "
               "and quantity</text>";
        for (int i = 0; i < 4; ++i) {
            const double price = range.low + static_cast<double>(i) * (range.high - range.low) / 3.0;
            out << "<path stroke=\"#e3e9ef\" d=\"M60 " << y(price) << "H740\"/><text x=\"3\" y=\"" << y(price)
                << "\" font-size=\"11\">" << price / 100.0 << "</text>";
        }
        for (const auto *side : {"bid_cents", "ask_cents"}) {
            out << "<polyline fill=\"none\" stroke=\""
                << (std::string_view{side} == "bid_cents" ? "#137f83" : "#b54a49")
                << "\" stroke-width=\"2\" points=\"";
            double last_price = 0;
            bool first = true;
            for (const auto &q : case_.at("quotes")) {
                const auto t = q.at("at_seconds").get<double>(), price = q.at(side).get<double>();
                if (!first)
                    out << x(t) << ',' << y(last_price) << ' ';
                out << x(t) << ',' << y(price) << ' ';
                last_price = price;
                first = false;
            }
            out << "\"/>";
        }
        const auto limit = case_.at("limit_cents").get<double>();
        out << "<path stroke=\"#8060a4\" stroke-dasharray=\"5 4\" d=\"M60 " << y(limit) << "H740\"/>";
        for (const auto &f : case_.at("fills"))
            out << "<circle fill=\"#183e68\" cx=\"" << x(f.at("source_seconds").get<double>()) << "\" cy=\""
                << y(f.at("price_cents").get<double>()) << "\" r=\"5\"><title>Quantity "
                << f.at("quantity").get<std::int64_t>() << "; receipt second "
                << f.at("receipt_seconds").get<std::int64_t>() << "</title></circle>";
        const auto deadline = case_.at("dispatch_seconds").get<double>() +
                              case_.at("policy").at("cancel_after_seconds").get<double>();
        out << "<path stroke=\"#bd8f1b\" stroke-dasharray=\"3 3\" d=\"M" << x(deadline)
            << " 45V245\"/><text x=\"" << x(deadline) + 3
            << "\" y=\"60\" font-size=\"10\">cancel deadline</text>";
        for (const auto t : {10, 15, 20, 25, 30})
            out << "<text x=\"" << x(t) << "\" y=\"260\" font-size=\"11\">" << t << "</text>";
        out << "<text x=\"240\" y=\"280\" font-size=\"12\">Virtual seconds since 2023-01-03 14:30:00 "
               "UTC</text></svg>";
        return out.str();
    }

  private:
    struct CentsRange {
        double low{1e9};
        double high{0};
    };
    const Json &case_;
};
} // namespace
void ExecutionMeasurementReport::write(const std::filesystem::path &directory) const {
    std::filesystem::create_directories(directory);
    const auto save = [&directory](std::string_view name, const std::string &bytes) {
        write_new_text_file(directory / name, bytes);
    };
    save("measurement_evidence.json", Json{{"cases", result_.cases},
                                           {"comparisons", result_.comparisons},
                                           {"sensitivities", result_.sensitivities},
                                           {"policy_summaries", result_.policy_summaries}}
                                              .dump() +
                                          '\n');
    save("source_inventory.txt", std::string{build::source_records});
    std::ostringstream csv;
    csv << "case,workload,policy,side,requested_quantity,filled_quantity,unfilled_quantity,fill_fraction,"
           "executed_shortfall_twice_cents,spread_component_twice_cents,timing_component_twice_cents,"
           "unfilled_opportunity_twice_cents,total_shortfall_twice_cents,total_shortfall_bps,acknowledgement_"
           "receipt_delay_seconds,completion_receipt_delay_seconds,completion_censored,completion_"
           "observation_seconds,completion_censor_reason,cancel_outcome\n";
    for (const auto &c : result_.cases) {
        const auto &m = c.at("metrics");
        csv << ReportText{c.at("case").get<std::string>()}.csv() << ','
            << ReportText{c.at("workload").at("id").get<std::string>()}.csv() << ','
            << ReportText{c.at("policy").at("id").get<std::string>()}.csv() << ','
            << c.at("workload").at("side").get<std::string>();
        for (const auto key :
             {"requested_quantity", "filled_quantity", "unfilled_quantity", "fill_fraction",
              "executed_shortfall_twice_cents", "spread_component_twice_cents",
              "timing_component_twice_cents", "unfilled_opportunity_twice_cents",
              "total_shortfall_twice_cents", "total_shortfall_bps", "acknowledgement_receipt_delay_seconds",
              "completion_receipt_delay_seconds", "completion_censored", "completion_observation_seconds",
              "completion_censor_reason"})
            csv << ',' << (m.at(key).is_null() ? "" : m.at(key).dump());
        csv << ',' << c.at("cancel_outcome").get<std::string>() << '\n';
    }
    save("execution_metrics.csv", csv.str());
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\">"
            "<title>Simulated execution measurements</title><style>body{font:15px "
            "system-ui;color:#193348;background:#f3f6fa;max-width:1250px;margin:2rem auto;padding:0 1rem}"
            "aside{padding:1rem;background:#fff0d2;border-left:4px solid "
            "#bc8515}table{border-collapse:collapse;width:100%;background:white;margin:1rem 0}"
            "td,th{padding:.6rem;text-align:left;border-bottom:1px solid "
            "#d8e2eb}details{padding:1rem;background:white;margin:.7rem "
            "0}summary{cursor:pointer;font-weight:600}"
            "svg{max-width:100%;height:auto}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px}."
            "scroll{overflow-x:auto}.good{color:#137f83}.cost{color:#b54a49}</style>"
            "<h1>Simulated execution measurements</h1><aside>All quotes, liquidity and message delays are "
            "synthetic. No historical SIP quotes, external broker executions or network requests. "
            "Prices and completion costs explain specified scenarios; they do not estimate real trading "
            "costs or market impact.</aside><p>"
         << result_.cases.size() << " account cases; " << result_.comparisons.size()
         << " matched differences against the cross_11s policy. "
            "Each case executes independently against the same workload inputs. Limits are fixed at the "
            "decision quote and never repriced.</p>"
            "<h2>Policy comparison</h2><p>Cross limits use the opposite touch; passive limits use the "
            "same-side touch. All measured orders contain ten shares. "
            "Buy and sell workloads are mirrored; sells start with separately journaled inventory. Quotes "
            "are immediately observable. Client dispatch is second 11, one second after the decision. "
            "Completion observation ends at second 30. Fees are explicitly zero.</p><div "
            "class=\"scroll\"><table><thead><tr><th>Policy</th><th>Cases</th><th>Filled shares / "
            "requested</th><th>Completed / censored</th><th>Canceled / cancel rejected</th><th>Total "
            "shortfall bps</th><th>Mean ack receipt s</th><th>Mean completion receipt s, completed cases "
            "only</th></tr></thead><tbody>";
    for (const auto &p : result_.policy_summaries)
        html << "<tr><td>" << ReportText{p.at("policy").get<std::string>()}.html() << "</td><td>"
             << p.at("case_count") << "</td><td>" << shown(p.at("fill_fraction")) << "</td><td>"
             << p.at("completed_cases") << " / " << p.at("censored_cases") << "</td><td>"
             << p.at("canceled_cases") << " / " << p.at("cancel_rejected_cases") << "</td><td>"
             << shown(p.at("total_shortfall_bps")) << "</td><td>"
             << shown(p.at("mean_acknowledgement_receipt_seconds")) << "</td><td>"
             << shown(p.at("mean_completion_receipt_seconds")) << "</td></tr>";
    html << "</tbody></table></div>";
    double maximum_component = 1.0;
    for (const auto &p : result_.policy_summaries)
        for (const auto key : {"spread_component_bps", "timing_component_bps", "unfilled_opportunity_bps"})
            maximum_component = std::max(maximum_component, std::abs(p.at(key).get<double>()));
    html << "<svg viewBox=\"0 0 1000 260\" role=\"img\" aria-label=\"Requested-notional cost components by "
            "policy\">"
            "<rect width=\"1000\" height=\"260\" fill=\"white\"/>"
            "<text x=\"20\" y=\"20\">Cost components, basis points of requested notional</text>"
            "<text x=\"20\" y=\"40\" font-size=\"12\">Green: spread | blue: timing | gold: unfilled "
            "opportunity. Bars share a zero line.</text>"
            "<path d=\"M470 50V230\" stroke=\"#9aabb9\"/>";
    std::size_t row = 0;
    for (const auto &p : result_.policy_summaries) {
        const auto y0 = 65 + row++ * 42;
        html << "<text x=\"20\" y=\"" << y0 + 14 << "\" font-size=\"13\">"
             << ReportText{p.at("policy").get<std::string>()}.html() << "</text>";
        std::size_t component = 0;
        for (const auto key : {"spread_component_bps", "timing_component_bps", "unfilled_opportunity_bps"}) {
            const auto value = p.at(key).get<double>();
            const auto width = std::abs(value) * 270.0 / maximum_component;
            html << "<rect x=\"" << (value < 0 ? 470.0 - width : 470.0) << "\" y=\"" << y0 + component * 9
                 << "\" width=\"" << width << "\" height=\"7\" fill=\""
                 << std::vector<std::string>{"#137f83", "#416aa4", "#bd8f1b"}.at(component) << "\"><title>"
                 << key << ": " << shown(p.at(key)) << " bps</title></rect>";
            ++component;
        }
        html << "<text x=\"770\" y=\"" << y0 + 14 << "\" font-size=\"12\">Total "
             << shown(p.at("total_shortfall_bps")) << " bps</text>";
    }
    for (const auto factor : {-1, 0, 1})
        html << "<text x=\"" << 470 + factor * 270 << "\" y=\"248\" font-size=\"11\">"
             << shown(Json(factor * maximum_component)) << " bps</text>";
    html << "</svg><p>Positive shortfall is a cost; negative is a benefit relative to the benchmark. Total "
            "shortfall includes unfilled shares marked at the common terminal midpoint. "
            "It is normalized by requested notional. Executed-only shortfall uses filled notional and is "
            "unavailable with no fills. Conditional completion delays omit censored cases; their counts "
            "remain visible. Incomplete observations stop at terminal cancellation receipt, or at the "
            "horizon if cancellation was not observed. Policy-driven cancellation is informative; no "
            "survival estimator is fitted. "
            "Scenario averages are descriptive and do not supply sampling confidence or a universal best "
            "policy.</p>"
            "<h2>Matched workload differences</h2><details><summary>Candidate minus cross_11s, same "
            "workload</summary><div "
            "class=\"scroll\"><table><thead><tr><th>Workload</th><th>Candidate</th><th>Fill fraction "
            "difference</th><th>Total shortfall difference USD</th></tr></thead><tbody>";
    for (const auto &p : result_.comparisons)
        html << "<tr><td>" << ReportText{p.at("workload").get<std::string>()}.html() << "</td><td>"
             << ReportText{p.at("candidate").get<std::string>()}.html() << "</td><td>"
             << shown(p.at("fill_fraction_difference")) << "</td><td>"
             << shown(Json(p.at("total_shortfall_difference_twice_cents").get<double>() / 200.0))
             << "</td></tr>";
    html << "</tbody></table></div></details><details><summary>Assumption changes against the base workload, "
            "same policy</summary>"
            "<p>Each difference changes one declared setting while preserving path, side, quantity and "
            "policy. These are scenario sensitivities, not estimated causal market effects.</p>"
            "<div class=\"scroll\"><table><thead><tr><th>Workload</th><th>Policy</th><th>Fill fraction "
            "difference</th><th>Total shortfall difference USD</th><th>Ack receipt difference "
            "s</th></tr></thead><tbody>";
    for (const auto &p : result_.sensitivities)
        html << "<tr><td>" << ReportText{p.at("workload").get<std::string>()}.html() << "</td><td>"
             << ReportText{p.at("policy").get<std::string>()}.html() << "</td><td>"
             << shown(p.at("fill_fraction_difference")) << "</td><td>"
             << shown(Json(p.at("total_shortfall_difference_twice_cents").get<double>() / 200.0))
             << "</td><td>" << p.at("acknowledgement_receipt_difference_seconds") << "</td></tr>";
    html << "</tbody></table></div></details><h2>Quote, fill and client-state inspection</h2><p>Green: bid. "
            "Red: ask. Purple dashed: fixed limit. Gold: local cancellation deadline. "
            "Source-time fill markers show quantity and receipt time on hover. Expand an event for the saved "
            "order state; no accounting is recomputed in this viewer.</p>";
    std::string workload;
    for (const auto &c : result_.cases) {
        const auto id = c.at("workload").at("id").get<std::string>();
        if (id != workload) {
            if (!workload.empty())
                html << "</details>";
            const bool first_workload = workload.empty();
            workload = id;
            html << (first_workload ? "<details open><summary>" : "<details><summary>")
                 << ReportText{id}.html() << "</summary><pre>" << ReportText{c.at("workload").dump(2)}.html()
                 << "</pre>";
        }
        const auto &m = c.at("metrics");
        html << (c.at("case") == result_.cases.at(0).at("case") ? "<details open><summary>"
                                                                : "<details><summary>")
             << ReportText{c.at("policy").at("id").get<std::string>()}.html() << " | filled "
             << m.at("filled_quantity") << "/10 | total shortfall " << shown(m.at("total_shortfall_bps"))
             << " bps</summary>" << MeasurementFigure{c}.svg()
             << "<p>Executed / spread / timing / opportunity, USD: "
             << shown(Json(m.at("executed_shortfall_twice_cents").get<double>() / 200.0)) << " / "
             << shown(Json(m.at("spread_component_twice_cents").get<double>() / 200.0)) << " / "
             << shown(Json(m.at("timing_component_twice_cents").get<double>() / 200.0)) << " / "
             << shown(Json(m.at("unfilled_opportunity_twice_cents").get<double>() / 200.0))
             << ".</p>"
                "<p>Cancellation outcome: "
             << ReportText{c.at("cancel_outcome").get<std::string>()}.html()
             << ".</p>"
                "<div class=\"scroll\"><table><thead><tr><th>Event</th><th>Source s</th><th>Receipt "
                "s</th><th>Incremental shares</th><th>Cumulative shares</th></tr></thead><tbody>";
        for (const auto &e : c.at("events")) {
            const auto data = Json::parse(e.at("raw").get<std::string>()).at("data");
            html << "<tr><td>" << ReportText{e.at("event").get<std::string>()}.html() << "</td><td>"
                 << e.at("source_seconds") << "</td><td>" << e.at("receipt_seconds") << "</td><td>"
                 << (data.contains("qty") ? data.at("qty").get<std::string>() : "") << "</td><td>"
                 << data.at("order").at("filled_qty").get<std::string>() << "</td></tr>";
        }
        html << "</tbody></table></div><details><summary>Client state and reservation "
                "timeline</summary><table><thead><tr><th>Event</th><th>Client s</th><th>Cash "
                "USD</th><th>Reserved USD</th><th>Shares / reserved sells</th></tr></thead><tbody>";
        for (const auto &f : c.at("frames")) {
            const auto &a = f.at("state").at("account");
            html << "<tr><td><details><summary>" << ReportText{f.at("event").get<std::string>()}.html()
                 << "</summary><pre>" << ReportText{f.at("state").at("orders").dump(2)}.html()
                 << "</pre></details></td><td>" << f.at("at_seconds") << "</td><td>"
                 << shown(Json(a.at("cash_units").get<double>() / 1e9)) << "</td><td>"
                 << shown(Json(a.at("reserved_cash_units").get<double>() / 1e9)) << "</td><td>"
                 << a.at("position_shares") << " / " << a.at("reserved_sell_shares") << "</td></tr>";
        }
        html << "</tbody></table></details></details>";
    }
    if (!workload.empty())
        html << "</details>";
    html << "<p><a href=\"execution_metrics.csv\">Metrics CSV</a> | <a "
            "href=\"measurement_evidence.json\">Full saved evidence</a> | <a href=\"summary.json\">Artifact "
            "manifest</a></p></html>\n";
    save("report.html", html.str());
    Json artifacts = Json::array();
    std::vector<std::filesystem::path> files;
    for (const auto &item : std::filesystem::recursive_directory_iterator(directory))
        if (item.is_regular_file())
            files.push_back(item.path());
    std::sort(files.begin(), files.end());
    for (const auto &file : files)
        artifacts.push_back({{"path", std::filesystem::relative(file, directory).generic_string()},
                             {"sha256", sha256_file(file)}});
    save("summary.json", Json{{"schema_version", 1},
                              {"evidence_origin", "synthetic"},
                              {"quote_feed", "synthetic_top_of_book_v1"},
                              {"clock_origin_utc", "2023-01-03T14:30:00Z"},
                              {"clock_resolution_seconds", 1},
                              {"case_count", result_.cases.size()},
                              {"paired_comparison_count", result_.comparisons.size()},
                              {"assumption_comparison_count", result_.sensitivities.size()},
                              {"network_requests_sent", 0},
                              {"broker_events_observed", 0},
                              {"fees", "zero"},
                              {"source_sha256", build::source_sha256},
                              {"compiler", TRADING_COMPILER_ID},
                              {"build_configuration", TRADING_BUILD_CONFIGURATION},
                              {"artifacts", artifacts}}
                                 .dump(2) +
                             '\n');
}
} // namespace trading
