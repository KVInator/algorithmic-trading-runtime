#include "cost_delay_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "return_statistics.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::pair<const char *, const char *>, 4> comparisons{
    {{"momentum", "momentum_always_long"},
     {"continuation_volume", "continuation_price"},
     {"reversion_candidate", "reversion_sign_only"},
     {"reversion_candidate", "reversion_drift"}}};
constexpr std::array<const char *, 4> endpoints{"momentum_increment", "continuation_increment",
                                                "reversion_sign_increment", "reversion_drift_increment"};
constexpr std::array<const char *, 8> policies{"momentum",
                                               "momentum_always_long",
                                               "continuation_price",
                                               "continuation_volume",
                                               "reversion_candidate",
                                               "reversion_sign_only",
                                               "reversion_drift",
                                               "reversion_always_long"};
Json statistics(const std::vector<double> &values) {
    if (values.empty()) {
        return nullptr;
    }
    const auto stats = describe_return_series(values);
    return {{"mean", stats.mean},
            {"eligible_date_product", stats.compounded},
            {"eligible_date_maximum_drawdown", stats.maximum_drawdown}};
}
Json record(const PanelReplayDay &day, const std::vector<CostDelayPath> &paths) {
    Json output = Json::array();
    for (const auto &path : paths) {
        const bool available = day.scored_period && day.common_operational && path.resolved;
        Json trades = Json::array(), costs = Json::array();
        for (const auto &trade : path.trades) {
            trades.push_back({{"entry_decision", format_utc_seconds(trade.entry_decision)},
                              {"entry_at", format_utc_seconds(trade.entry_reference_time)},
                              {"exit_decision", format_utc_seconds(trade.exit_decision)},
                              {"exit_at", format_utc_seconds(trade.exit_reference_time)},
                              {"entry_price", trade.entry_reference_price},
                              {"exit_price", trade.exit_reference_price}});
        }
        for (double rate : {0.0, .5, 1.0, 2.0, 5.0}) {
            const auto found = std::find_if(path.outcomes.begin(), path.outcomes.end(),
                                            [&](const auto &item) { return item.cost_bps == rate; });
            const auto value = [&](double CostDelayOutcome::*member) -> Json {
                return available ? Json((*found).*member) : Json(nullptr);
            };
            costs.push_back(
                {{"cost_bps", rate},
                 {"gross", value(&CostDelayOutcome::gross_return)},
                 {"net", value(&CostDelayOutcome::net_return)},
                 {"traded_notional_per_initial_equity", value(&CostDelayOutcome::traded_notional)},
                 {"cost_per_initial_equity", value(&CostDelayOutcome::cost_paid)}});
        }
        output.push_back({{"policy", path.policy},
                          {"delay_minutes", path.delay_minutes},
                          {"resolved", path.resolved},
                          {"reason", path.reason},
                          {"outcome_available", available},
                          {"completed_trades", trades},
                          {"minutes_held", available ? Json(path.minutes_held) : Json(nullptr)},
                          {"costs", costs}});
    }
    return {{"primary", panel_replay_record(day)}, {"paths", output}};
}
void curve(std::ostream &html, const Json &rows, std::string_view symbol, std::string_view name,
           bool paired) {
    double low = 0, high = 0;
    bool observed = false;
    for (const auto &row : rows) {
        if (row.at("symbol").get<std::string>() == symbol &&
            row.at(paired ? "endpoint" : "policy").get<std::string>() == name &&
            !row.at("statistics").is_null()) {
            const double y = 10000 * row.at("statistics").at("mean").get<double>();
            observed = true;
            low = std::min(low, y);
            high = std::max(high, y);
        }
    }
    const double padding = std::max(.1, (high - low) * .12);
    low -= padding;
    high += padding;
    const auto y = [&](double v) { return 225 - (v - low) * 185 / (high - low); };
    html << std::setprecision(10) << "<svg viewBox=\"0 0 720 285\" role=\"img\" aria-label=\""
         << ReportText{std::string{symbol} + " " + std::string{name} + " cost sensitivity"}.html()
         << "\"><line x1=\"85\" x2=\"685\" y1=\"" << y(0) << "\" y2=\"" << y(0)
         << "\" stroke=\"#a5b5c4\"/><text x=\"85\" y=\"18\">"
         << (paired ? "Candidate minus control (bp/day)" : "Mean net return (bp/day)") << "</text>";
    for (int tick = 0; tick <= 4; ++tick) {
        const double value = low + (high - low) * tick / 4;
        html << "<text x=\"75\" y=\"" << y(value) + 4 << "\" text-anchor=\"end\">" << std::fixed
             << std::setprecision(2) << value << "</text>";
    }
    for (double rate : {0.0, .5, 1.0, 2.0, 5.0}) {
        html << "<text x=\"" << 85 + rate * 120 << "\" y=\"249\" text-anchor=\"middle\">" << rate
             << "</text>";
    }
    html << "<text x=\"385\" y=\"277\" text-anchor=\"middle\">All-in cost per side (bp)</text>";
    for (int delay : {1, 2}) {
        const char *color = delay == 1 ? "#116b99" : "#b05220";
        std::ostringstream points;
        bool any = false;
        for (const auto &row : rows) {
            if (row.at("symbol").get<std::string>() != symbol ||
                row.at(paired ? "endpoint" : "policy").get<std::string>() != name ||
                row.at("delay_minutes").get<int>() != delay || row.at("statistics").is_null()) {
                continue;
            }
            const double value = 10000 * row.at("statistics").at("mean").get<double>();
            const double x = 85 + 120 * row.at("cost_bps").get<double>();
            points << std::fixed << std::setprecision(6) << x << ',' << y(value) << ' ';
            any = true;
            html << "<circle data-series=\""
                 << ReportText{std::string{symbol} + '/' + std::string{name}}.html() << "\" data-delay=\""
                 << delay << "\" data-cost=\"" << row.at("cost_bps") << "\" data-mean-bp=\""
                 << std::setprecision(10) << value << "\" cx=\"" << x << "\" cy=\"" << y(value)
                 << "\" r=\"3\" fill=\"" << color << "\"/>";
        }
        if (any) {
            html << "<polyline points=\"" << points.str() << "\" stroke=\"" << color
                 << "\" fill=\"none\" stroke-width=\"2\"/>";
        }
        html << "<text x=\"" << (delay == 1 ? 380 : 530) << "\" y=\"35\" fill=\"" << color << "\">" << delay
             << " minute delay</text>";
    }
    if (!observed) {
        html << "<text x=\"385\" y=\"125\" text-anchor=\"middle\">No eligible observations; outcome "
                "unavailable</text>";
    }
    html << "</svg>";
}
std::string report_html(const Json &summary) {
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\">"
         << "<title>Historical cost and delay comparison</title><style>body{font:16px/1.5 "
            "system-ui;color:#18354a;background:#f4f7fa;margin:24px}"
         << "main{max-width:1150px;margin:auto}section{background:white;padding:20px;margin:18px "
            "0;border:1px solid #d9e2e9;border-radius:8px}"
         << "svg{width:100%;max-height:350px}table{border-collapse:collapse;width:100%}th,td{padding:7px;"
            "border-bottom:1px solid #d9e2e9;text-align:right}"
         << "th:first-child,td:first-child{text-align:left}select{padding:8px;max-width:100%}pre{white-space:"
            "pre-wrap;overflow-wrap:anywhere}"
         << ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(420px,1fr));gap:12px}</"
            "style><main>"
         << "<h1>Historical cost and delay comparison</h1><p><strong>Evidence origin: "
         << summary.at("evidence_origin").get<std::string>()
         << "</strong>. Fixed primary decisions; 0, 0.5, 1, 2 and 5 bp per side; one- and two-minute "
            "observed-open references.</p>"
         << "<p>Fractional, fully invested intraday reference returns. No fill, whole-share shared-account "
            "or market-impact claim. "
         << "The common operational mask is applied after decisions. Missing outcomes remain NA. "
         << "Products and drawdowns describe eligible dates only; they are not a continuous account across "
            "excluded dates.</p>"
         << "<section><h2>Calendar coverage</h2><table><tr><th>Year</th><th>Calendar dates</th><th>Common "
            "operational dates</th><th>Coverage</th></tr>";
    for (const auto &row : summary.at("coverage")) {
        const auto total = row.at("calendar_dates").get<double>();
        html << "<tr><td>" << row.at("year").get<std::string>() << "</td><td>" << row.at("calendar_dates")
             << "</td><td>" << row.at("common_dates") << "</td><td>" << std::fixed << std::setprecision(1)
             << 100 * row.at("common_dates").get<double>() / total << "%</td></tr>";
    }
    html << "</table></section><h2>Equal-weight paired effects</h2><p>Each point averages the five "
            "instrument-level candidate-minus-control returns on identical dates. "
         << "This is a research contrast, not a shared-capital portfolio. Uncertainty and risk matching "
            "remain M7 work.</p><div class=\"grid\">";
    for (const auto *endpoint : endpoints) {
        html << "<section><h3>" << endpoint << "</h3>";
        curve(html, summary.at("paired"), "equal_weight", endpoint, true);
        html << "</section>";
    }
    html << "</div><h2>Instrument and policy detail</h2><label for=\"detail\">View </label><select "
            "id=\"detail\">";
    for (const auto *symbol : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        for (const auto *policy : policies) {
            html << "<option>" << symbol << '/' << policy << "</option>";
        }
    }
    html << "</select><p>All charts and tables are embedded. The selector changes visibility only; "
            "script-disabled viewers retain every detail.</p>";
    for (const auto *symbol : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        for (const auto *policy : policies) {
            html << "<section data-detail=\"" << symbol << '/' << policy << "\"><h3>" << symbol << " | "
                 << policy << "</h3>";
            curve(html, summary.at("series"), symbol, policy, false);
            html << "<table><tr><th>Delay / bp per side</th><th>Dates</th><th>Active dates</th><th>Net "
                    "bp/day</th><th>Eligible-date product %</th><th>Max drawdown %</th><th>Notional / NAV / "
                    "day</th></tr>";
            for (const auto &row : summary.at("series")) {
                if (row.at("symbol").get<std::string>() != symbol || row.at("policy") != policy) {
                    continue;
                }
                html << "<tr data-case=\"" << symbol << '/' << policy << '/' << row.at("delay_minutes") << '/'
                     << row.at("cost_bps") << "\"><td>" << row.at("delay_minutes") << " / "
                     << row.at("cost_bps") << "</td><td>" << row.at("dates") << "</td><td>"
                     << row.at("active_dates") << "</td>";
                if (row.at("statistics").is_null()) {
                    html << "<td>NA</td><td>NA</td><td>NA</td><td>NA</td>";
                } else {
                    const auto &s = row.at("statistics");
                    html << std::fixed << std::setprecision(6) << "<td>" << s.at("mean").get<double>() * 10000
                         << "</td><td>" << s.at("eligible_date_product").get<double>() * 100 << "</td><td>"
                         << s.at("eligible_date_maximum_drawdown").get<double>() * 100 << "</td><td>"
                         << row.at("mean_traded_notional_per_initial_equity").get<double>() << "</td>";
                }
                html << "</tr>";
            }
            html << "</table></section>";
        }
    }
    html << "<script>const view=document.getElementById('detail');function "
            "show(){document.querySelectorAll('[data-detail]').forEach(s=>s.hidden=s.dataset.detail!==view."
            "value)}view.addEventListener('change',show);show();</script>"
         << "<p>No quote eligibility was changed or quote-based charge added. Synthetic matching, continuous "
            "daily-risk accounts and simultaneous inference remain separate evaluations.</p>"
         << "<p><a href=\"summary.json\">Evidence manifest</a></p></main></html>";
    return html.str();
}
} // namespace
CostDelayReport::CostDelayReport(const std::filesystem::path &root, Json provenance,
                                 std::filesystem::path dataset, CostDelayStudy study)
    : directory_(create_unique_directory(
          root, provenance.at("evidence_origin") == "synthetic" ? "cost_delay_check" : "cost_delay")),
      study_(std::move(study)),
      summary_{{"schema_version", 1},
               {"kind", "cost_delay_comparison"},
               {"evidence_origin", provenance.at("evidence_origin")},
               {"provenance", std::move(provenance)},
               {"input_dataset", dataset.generic_string()},
               {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
               {"costs_bps_per_side", study_.costs()},
               {"delays_minutes", study_.delays()},
               {"primary_cost_bps", 1},
               {"primary_delay_minutes", 1},
               {"signals_refitted_for_costs", false},
               {"primary_reference_parity_passed", true},
               {"reserved_strategy_inputs_opened", false},
               {"network_requests", 0},
               {"broker_events", 0},
               {"shared_capital_replayed", false},
               {"daily_risk_accounts_replayed", false},
               {"synthetic_execution_cases_executed", 0},
               {"simultaneous_inference_completed", false},
               {"path_interpretation", "eligible_date_product_not_continuous_account"},
               {"source_sha256", TRADING_SOURCE_SHA256},
               {"compiler", TRADING_COMPILER_ID},
               {"build_configuration", TRADING_BUILD_CONFIGURATION}} {
    for (const auto *symbol : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        for (const auto *policy : policies) {
            for (int delay : study_.delays()) {
                for (double cost : study_.costs()) {
                    series_[{symbol, policy, delay, cost}];
                }
            }
        }
        for (const auto *endpoint : endpoints) {
            for (int delay : study_.delays()) {
                for (double cost : study_.costs()) {
                    pairs_[{symbol, endpoint, delay, cost}];
                }
            }
        }
    }
    for (const auto *endpoint : endpoints) {
        for (int delay : study_.delays()) {
            for (double cost : study_.costs()) {
                pairs_[{"equal_weight", endpoint, delay, cost}];
            }
        }
    }
}
const std::filesystem::path &CostDelayReport::directory() const noexcept { return directory_; }
void CostDelayReport::append(std::span<const PanelReplayDay> days,
                             std::span<const SessionDownload> sessions) {
    if (finished_ || days.size() != 5 || sessions.size() != 5 || days.front().date <= previous_date_) {
        throw std::invalid_argument("cost/delay writer requires ordered five-instrument dates");
    }
    const auto date = days.front().date;
    const bool common =
        std::all_of(days.begin(), days.end(), [](const auto &day) { return day.availability.operational; });
    std::set<std::string> seen;
    std::map<std::tuple<std::string, int, double>, double> pooled;
    for (const auto &day : days) {
        if (day.date != date || day.common_operational != common || !seen.insert(day.symbol).second ||
            !series_.contains({day.symbol, "momentum", 1, 0})) {
            throw std::invalid_argument("cost/delay batch mixed dates, masks or instruments");
        }
        const auto session = std::find_if(sessions.begin(), sessions.end(), [&](const auto &item) {
            return item.request.symbol() == day.symbol;
        });
        if (session == sessions.end()) {
            throw std::invalid_argument("cost/delay session is missing");
        }
        const auto paths = study_.evaluate(day, *session);
        std::map<std::tuple<std::string, int, double>, double> returns;
        for (const auto &path : paths) {
            if (!day.scored_period || !common || !path.resolved) {
                continue;
            }
            for (const auto &outcome : path.outcomes) {
                auto &series = series_.at({day.symbol, path.policy, path.delay_minutes, outcome.cost_bps});
                series.values.push_back(outcome.net_return);
                series.active += !path.trades.empty();
                series.trades += path.trades.size();
                series.turnover += outcome.traded_notional;
                series.cost_paid += outcome.cost_paid;
                series.minutes_held += static_cast<double>(path.minutes_held);
                returns[{path.policy, path.delay_minutes, outcome.cost_bps}] = outcome.net_return;
            }
        }
        if (day.scored_period && common) {
            for (std::size_t i = 0; i < endpoints.size(); ++i) {
                for (int delay : study_.delays()) {
                    for (double cost : study_.costs()) {
                        const auto difference = returns.at({comparisons[i].first, delay, cost}) -
                                                returns.at({comparisons[i].second, delay, cost});
                        pairs_.at({day.symbol, endpoints[i], delay, cost}).values.push_back(difference);
                        pooled[{endpoints[i], delay, cost}] += difference / 5;
                    }
                }
            }
        }
        const auto line = record(day, paths).dump() + '\n';
        auto &output = outputs_[day.symbol];
        if (!output.stream.is_open()) {
            output.stream.exceptions(std::ios::failbit | std::ios::badbit);
            output.stream.open(directory_ / (day.symbol + ".jsonl.tmp"), std::ios::binary | std::ios::trunc);
        }
        if (line.size() > 256U * 1024U || output.bytes + line.size() > 128U * 1024U * 1024U) {
            throw std::invalid_argument("cost/delay evidence exceeds its record/file bound");
        }
        output.stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        output.bytes += line.size();
        ++output.records;
        output.scored += day.scored_period;
        output.common += day.scored_period && common;
    }
    for (const auto &[key, value] : pooled) {
        const auto &[endpoint, delay, cost] = key;
        pairs_.at({"equal_weight", endpoint, delay, cost}).values.push_back(value);
    }
    if (days.front().scored_period) {
        auto &count = coverage_[date.substr(0, 4)];
        ++count.first;
        count.second += common;
    }
    previous_date_ = date;
}
std::filesystem::path CostDelayReport::finish(Json checks) {
    if (finished_ || outputs_.size() != 5) {
        throw std::logic_error("cost/delay report requires five instrument streams");
    }
    Json artifacts = Json::array(), counts = Json::array(), series = Json::array(), pairs = Json::array(),
         coverage = Json::array();
    for (auto &[symbol, output] : outputs_) {
        output.stream.close();
        const auto name = symbol + ".jsonl";
        std::filesystem::rename(directory_ / (name + ".tmp"), directory_ / name);
        artifacts.push_back({{"path", name},
                             {"bytes", output.bytes},
                             {"sha256", sha256_file(directory_ / name, 128U * 1024U * 1024U)}});
        counts.push_back({{"symbol", symbol},
                          {"calendar_records", output.records},
                          {"development_records", output.scored},
                          {"common_operational_records", output.common}});
    }
    for (const auto &[key, value] : series_) {
        const auto &[symbol, policy, delay, cost] = key;
        const auto n = value.values.size();
        series.push_back(
            {{"symbol", symbol},
             {"policy", policy},
             {"delay_minutes", delay},
             {"cost_bps", cost},
             {"dates", n},
             {"active_dates", value.active},
             {"completed_trades", value.trades},
             {"statistics", statistics(value.values)},
             {"mean_traded_notional_per_initial_equity",
              n ? Json(value.turnover / static_cast<double>(n)) : Json(nullptr)},
             {"mean_cost_per_initial_equity",
              n ? Json(value.cost_paid / static_cast<double>(n)) : Json(nullptr)},
             {"mean_minutes_held", n ? Json(value.minutes_held / static_cast<double>(n)) : Json(nullptr)}});
    }
    for (const auto &[key, value] : pairs_) {
        const auto &[symbol, endpoint, delay, cost] = key;
        // An increment is a contrast, not an investable return; do not compound it.
        long double sum = 0;
        for (double v : value.values) {
            sum += v;
        }
        pairs.push_back(
            {{"symbol", symbol},
             {"endpoint", endpoint},
             {"delay_minutes", delay},
             {"cost_bps", cost},
             {"dates", value.values.size()},
             {"statistics", value.values.empty()
                                ? Json(nullptr)
                                : Json{{"mean", static_cast<double>(sum / value.values.size())}}}});
    }
    for (const auto &[year, count] : coverage_) {
        coverage.push_back({{"year", year}, {"calendar_dates", count.first}, {"common_dates", count.second}});
    }
    summary_["artifacts"] = artifacts;
    summary_["instruments"] = counts;
    summary_["coverage"] = coverage;
    summary_["series"] = series;
    summary_["paired"] = pairs;
    summary_["series_count"] = series.size();
    summary_["paired_count"] = pairs.size();
    summary_["checks"] = std::move(checks);
    write_new_text_file(directory_ / "report.html", report_html(summary_));
    summary_["report_sha256"] = sha256_file(directory_ / "report.html", 16U * 1024U * 1024U);
    write_new_text_file(directory_ / "summary.json", summary_.dump(2) + '\n');
    finished_ = true;
    return directory_;
}
} // namespace trading
