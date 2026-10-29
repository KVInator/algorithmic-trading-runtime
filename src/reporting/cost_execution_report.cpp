#include "cost_execution_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
Json comparisons(const Json &rows) {
    Json result = Json::array();
    for (const auto &row : rows) {
        if (row.at("policy").at("id").get<std::string>() == "cross_5s") {
            continue;
        }
        const auto base = std::find_if(rows.begin(), rows.end(), [&](const auto &x) {
            return x.at("workload").at("id") == row.at("workload").at("id") &&
                   x.at("policy").at("id").template get<std::string>() == "cross_5s";
        });
        if (base == rows.end()) {
            throw std::invalid_argument("execution comparison omitted its matched reference");
        }
        result.push_back(
            {{"case_id", row.at("id")},
             {"reference_case_id", base->at("id")},
             {"workload_id", row.at("workload").at("id")},
             {"policy_id", row.at("policy").at("id")},
             {"fill_fraction_difference", row.at("metrics").at("fill_fraction").get<double>() -
                                              base->at("metrics").at("fill_fraction").get<double>()},
             {"total_shortfall_bps_difference",
              row.at("metrics").at("total_shortfall_bps").get<double>() -
                  base->at("metrics").at("total_shortfall_bps").get<double>()}});
    }
    return result;
}
Json policy_summaries(const Json &rows) {
    Json result = Json::array();
    for (const auto &p : execution::ExecutionMeasurementStudy::policies()) {
        std::size_t n = 0, censored = 0;
        std::int64_t filled = 0, requested = 0, total2 = 0, denom = 0;
        long double fraction = 0, shortfall = 0;
        for (const auto &row : rows) {
            if (row.at("policy").at("id").get<std::string>() == p.id) {
                const auto &m = row.at("metrics");
                ++n;
                filled += m.at("filled_quantity").get<std::int64_t>();
                requested += m.at("requested_quantity").get<std::int64_t>();
                total2 += m.at("total_shortfall_twice_units").get<std::int64_t>();
                denom += m.at("requested_quantity").get<std::int64_t>() *
                         m.at("arrival_midpoint_twice_units").get<std::int64_t>();
                fraction += m.at("fill_fraction").get<double>();
                shortfall += m.at("total_shortfall_bps").get<double>();
                censored += m.at("completion_censored").get<bool>();
            }
        }
        if (n != 170) {
            throw std::logic_error("policy matrix does not contain 170 matched workloads");
        }
        result.push_back({{"policy_id", p.id},
                          {"cases", n},
                          {"requested_quantity", requested},
                          {"filled_quantity", filled},
                          {"censored_cases", censored},
                          {"mean_fill_fraction", static_cast<double>(fraction / n)},
                          {"quantity_weighted_fill_fraction",
                           static_cast<double>(filled) / static_cast<double>(requested)},
                          {"mean_total_shortfall_bps", static_cast<double>(shortfall / n)},
                          {"notional_weighted_total_shortfall_bps",
                           10000.0 * static_cast<double>(total2) / static_cast<double>(denom)}});
    }
    return result;
}
std::string html(const Json &summary, const Json &rows) {
    std::ostringstream h;
    h << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
         "content=\"width=device-width,initial-scale=1\">"
      << "<title>Synthetic execution stress study</title><style>body{font:16px/1.5 "
         "system-ui;color:#193549;background:#f3f6fa;margin:24px}main{max-width:1200px;margin:auto}"
      << "section{background:white;border:1px solid #d5dfe7;border-radius:8px;padding:18px;margin:20px "
         "0;overflow:auto}svg{width:100%}table{border-collapse:collapse;width:100%;font-size:14px}"
      << "th,td{padding:7px;border-bottom:1px solid "
         "#d5dfe7;text-align:right}th:first-child,td:first-child{text-align:left}select{padding:8px}"
      << "pre{white-space:pre-wrap;overflow-wrap:anywhere}"
      << "@media "
         "print{table{font-size:12px;table-layout:fixed}th,td{padding:5px;overflow-wrap:anywhere}section{"
         "overflow:visible}}"
      << "</style><main><h1>Synthetic execution stress "
         "study</h1>"
      << "<p><strong>Evidence origin: synthetic</strong>. 680 registered account cases; 170 workloads; four "
         "limit/deadline policies. No market data or network service.</p>"
      << "<p>The existing five-symbol broker matcher owns its account independently of the client. Every "
         "case ends with exact reconciliation. This run uses in-process broker state; "
      << "the separate-process and restart evidence belongs to M5. No new process, persistence or "
         "external-broker claim is made here.</p>"
      << "<p>All five instruments share the same constructed price and liquidity paths. Symbol repetition "
         "checks routing symmetry, not independent market evidence. "
      << "One-factor stresses do not measure joint liquidity/delay interactions. Equal-case means are "
         "summaries of this fixed matrix, not estimated market expectations.</p>"
      << "<section><h2>Matched policy summaries</h2><table><tr><th>Policy</th><th>Cases</th><th>Mean fill "
         "%</th><th>Quantity-weighted fill %</th><th>Censored cases</th><th>Mean total shortfall "
         "bp</th><th>Notional-weighted shortfall bp</th></tr>";
    for (const auto &p : summary.at("policy_summaries")) {
        h << std::fixed << std::setprecision(6) << "<tr data-policy=\""
          << p.at("policy_id").get<std::string>() << "\"><td>" << p.at("policy_id").get<std::string>()
          << "</td><td>" << p.at("cases") << "</td><td>" << 100 * p.at("mean_fill_fraction").get<double>()
          << "</td><td>" << 100 * p.at("quantity_weighted_fill_fraction").get<double>() << "</td><td>"
          << p.at("censored_cases") << "</td><td>" << p.at("mean_total_shortfall_bps").get<double>()
          << "</td><td>" << p.at("notional_weighted_total_shortfall_bps").get<double>() << "</td></tr>";
    }
    h << "</table><p>Total shortfall includes the explicit fee and the terminal opportunity cost of unfilled "
         "shares. Zero shortfall with zero fills on a steady path is not successful execution.</p></section>"
      << "<h2>Fixed workload/policy matrix</h2><label for=\"market\">Instrument and side </label><select "
         "id=\"market\">";
    for (const auto *sym : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        for (const auto *side : {"buy", "sell"}) {
            h << "<option>" << sym << '/' << side << "</option>";
        }
    }
    h << "</select><p>Cell colour and width show fill fraction; labels show fill percentage and total "
         "shortfall in bp. All ten panels are embedded, including without JavaScript.</p>";
    for (const auto *sym : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        for (const auto *side : {"buy", "sell"}) {
            h << "<section data-panel=\"" << sym << '/' << side << "\"><h3>" << sym << " | " << side
              << "</h3><svg viewBox=\"0 0 1100 755\" role=\"img\" aria-label=\"Fill fraction and total "
                 "shortfall matrix\">";
            const auto policies = execution::ExecutionMeasurementStudy::policies();
            for (std::size_t c = 0; c < policies.size(); ++c) {
                h << "<text x=\"" << 330 + 200 * c << "\" y=\"22\" text-anchor=\"middle\">" << policies[c].id
                  << "</text>";
            }
            std::size_t r = 0;
            for (const auto &w : summary.at("plan").at("workloads")) {
                if (w.at("symbol").get<std::string>() != sym || w.at("side").get<std::string>() != side) {
                    continue;
                }
                const auto prefix = std::string{sym} + '_' + side + '_';
                h << "<text x=\"10\" y=\"" << 60 + 41 * r << "\">"
                  << ReportText{w.at("id").get<std::string>().substr(prefix.size())}.html() << "</text>";
                for (std::size_t c = 0; c < policies.size(); ++c) {
                    const auto id = w.at("id").get<std::string>() + '_' + policies[c].id;
                    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &x) {
                        return x.at("id").template get<std::string>() == id;
                    });
                    const auto &m = it->at("metrics");
                    const auto f = m.at("fill_fraction").get<double>();
                    const auto cost = m.at("total_shortfall_bps").get<double>();
                    h << "<rect x=\"" << 235 + 200 * c << "\" y=\"" << 37 + 41 * r
                      << "\" width=\"190\" height=\"34\" fill=\"#e6edf3\"/>"
                      << "<rect data-case=\"" << id << "\" data-fill=\"" << std::setprecision(12) << f
                      << "\" data-shortfall=\"" << cost << "\" x=\"" << 235 + 200 * c << "\" y=\""
                      << 37 + 41 * r << "\" width=\"" << 190 * f
                      << "\" height=\"34\" fill=\"#95cbbd\"/><text data-label=\"" << id << "\" x=\""
                      << 330 + 200 * c << "\" y=\"" << 60 + 41 * r << "\" text-anchor=\"middle\">"
                      << std::setprecision(1) << f * 100 << "% | " << std::setprecision(3) << cost
                      << " bp</text>";
                }
                ++r;
            }
            h << "</svg></section>";
        }
    }
    h << "<h2>Cost and clock conventions</h2><p>Arrival is the common decision/dispatch midpoint at second "
         "10; horizon is second 30. "
      << "Buy/sell limits use the decision quote's opposite touch for aggressive policy and same-side touch "
         "for passive policy. Limits remain fixed. "
      << "Freshness uses the two-second broker limit. Fills may precede cancellation confirmation. "
         "Completion is censored when any requested shares remain unfilled.</p>"
      << "<p>The engineering fee is USD 0.00001 per filled share. Selling cases acquire the requested "
         "inventory at second 1; those setup fills and fees are excluded from measured costs, "
      << "but retained in both ledgers. Filled prices, spread cost, price timing, fees and unfilled terminal "
         "opportunity cost are retained separately in the evidence.</p>"
      << "<script>const market=document.getElementById('market');function "
         "show(){document.querySelectorAll('[data-panel]').forEach(p=>p.hidden=p.dataset.panel!==market."
         "value)}market.addEventListener('change',show);show();</script>"
      << "<p><a href=\"summary.json\">Manifest and paired comparisons</a> | <a "
         "href=\"execution_cases.jsonl\">Full synthetic cases</a></p></main></html>";
    return h.str();
}
} // namespace
CostExecutionReport::CostExecutionReport(const std::filesystem::path &root)
    : directory_(create_unique_directory(root, "cost_execution")),
      plan_(execution::CostExecutionStudy{}.plan()) {
    stream_.exceptions(std::ios::failbit | std::ios::badbit);
    stream_.open(directory_ / "execution_cases.jsonl.tmp", std::ios::binary | std::ios::trunc);
}
const std::filesystem::path &CostExecutionReport::directory() const noexcept { return directory_; }
void CostExecutionReport::append(const Json &record) {
    if (finished_ || rows_.size() >= 680) {
        throw std::logic_error("cost execution report is closed or full");
    }
    const auto line = record.dump() + '\n';
    if (line.size() > 128U * 1024U || bytes_ + line.size() > 128U * 1024U * 1024U) {
        throw std::invalid_argument("synthetic execution evidence exceeds its explicit bound");
    }
    stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
    bytes_ += line.size();
    rows_.push_back({{"id", record.at("id")},
                     {"workload", record.at("workload")},
                     {"policy", record.at("policy")},
                     {"metrics", record.at("metrics")}});
}
std::filesystem::path CostExecutionReport::finish() {
    if (finished_ || rows_.size() != 680) {
        throw std::logic_error("synthetic execution report requires all 680 cases");
    }
    std::set<std::string> seen;
    for (const auto &r : rows_) {
        if (!seen.insert(r.at("id").get<std::string>()).second) {
            throw std::logic_error("duplicate synthetic case");
        }
    }
    stream_.close();
    std::filesystem::rename(directory_ / "execution_cases.jsonl.tmp", directory_ / "execution_cases.jsonl");
    Json s{{"schema_version", 1},
           {"kind", "cost_execution_study"},
           {"evidence_origin", "synthetic"},
           {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
           {"broker_policy_sha256", TRADING_BROKER_PROTOCOL_SHA256},
           {"plan", plan_},
           {"plan_sha256", sha256(plan_.dump())},
           {"case_count", 680},
           {"reconciled_cases", 680},
           {"network_requests", 0},
           {"external_broker_events", 0},
           {"native_child_processes", 0},
           {"historical_strategy_scored", false},
           {"reserved_strategy_inputs_opened", false},
           {"source_sha256", TRADING_SOURCE_SHA256},
           {"compiler", TRADING_COMPILER_ID},
           {"build_configuration", TRADING_BUILD_CONFIGURATION},
           {"policy_summaries", policy_summaries(rows_)},
           {"comparisons", comparisons(rows_)},
           {"artifacts", Json::array({{{"path", "execution_cases.jsonl"},
                                       {"bytes", bytes_},
                                       {"sha256", sha256_file(directory_ / "execution_cases.jsonl",
                                                              128U * 1024U * 1024U)}}})}};
    s["paired_comparison_count"] = s.at("comparisons").size();
    write_new_text_file(directory_ / "report.html", html(s, rows_));
    s["report_sha256"] = sha256_file(directory_ / "report.html", 16U * 1024U * 1024U);
    write_new_text_file(directory_ / "summary.json", s.dump(2) + '\n');
    finished_ = true;
    return directory_;
}
} // namespace trading
