#include "stability_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "cost_delay_report.hpp"
#include "panel_account_report.hpp"
#include "panel_replay_diagnostic.hpp"
#include "report_text.hpp"
#include "stability_dataset.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string text(const Json &value) { return ReportText{value.get<std::string>()}.html(); }
std::string numeric(const Json &value, double scale = 1) {
    if (value.is_null()) {
        return "NA";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value.get<double>() * scale;
    return out.str();
}
Json session_calendar_checks() {
    const auto empty = [](const char *date) {
        return Json{{"primary",
                     {{"date", date},
                      {"policies", Json::array()},
                      {"common_operational", false},
                      {"availability", {{"operational", false}}}}},
                    {"paths", Json::array()}};
    };
    const auto short_day = empty("2017-07-03");
    auto cash = empty("2017-07-05");
    cash["primary"]["common_operational"] = true;
    cash["primary"]["availability"]["operational"] = true;
    for (const auto *policy :
         {"momentum", "momentum_always_long", "continuation_price", "continuation_volume",
          "reversion_candidate", "reversion_sign_only", "reversion_drift", "reversion_always_long"}) {
        cash["primary"]["policies"].push_back({{"policy", policy}});
        cash["paths"].push_back({{"policy", policy},
                                 {"delay_minutes", 1},
                                 {"resolved", true},
                                 {"outcome_available", true},
                                 {"minutes_held", 0},
                                 {"completed_trades", Json::array()},
                                 {"costs", Json::array({{{"cost_bps", 1},
                                                         {"net", 0.0},
                                                         {"gross", 0.0},
                                                         {"traded_notional_per_initial_equity", 0.0},
                                                         {"cost_per_initial_equity", 0.0}}})}});
    }
    Json cases = Json::array();
    const auto check = [&](const char *name, const Json &record, bool expected) {
        Json observations = nullptr;
        bool accepted = false;
        try {
            const auto values = StabilityDataset::intraday_observations(record);
            observations = Json::object();
            for (const auto &[policy, value] : values) {
                observations[policy] = stability_observation_json(value);
            }
            accepted = true;
        } catch (const std::invalid_argument &) {
        }
        if (accepted != expected) {
            throw std::logic_error(std::string{"session calendar check failed: "} + name);
        }
        cases.push_back({{"case", name},
                         {"record", record},
                         {"accepted", accepted},
                         {"observations", observations}});
    };
    check("independence_day_early_close", short_day, true);
    check("thanksgiving_early_close", empty("2017-11-24"), true);
    check("regular_cash_is_observed", cash, true);
    check("regular_empty_paths_rejected", empty("2017-07-05"), false);
    auto changed = cash;
    changed["paths"].erase(0);
    check("regular_missing_policy_rejected", changed, false);
    changed = short_day;
    changed["primary"]["common_operational"] = true;
    check("short_common_mask_rejected", changed, false);
    changed = short_day;
    changed["paths"] = cash.at("paths");
    check("short_paths_rejected", changed, false);
    changed = short_day;
    changed["primary"]["policies"] = cash.at("primary").at("policies");
    check("short_policies_rejected", changed, false);
    changed = short_day;
    changed["primary"]["availability"]["operational"] = true;
    check("short_operational_rejected", changed, false);
    return cases;
}
class StabilityFigures {
  public:
    explicit StabilityFigures(const Json &summary) : summary_(summary) {}
    std::string html() const {
        std::ostringstream h;
        h << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\">"
             "<title>Primary stability and uncertainty</title><style>body{font:16px/1.55 "
             "system-ui;background:#f1f5f8;color:#193549;margin:24px}"
             "main{max-width:1180px;margin:auto}section{background:white;border:1px solid "
             "#d4dfe7;border-radius:8px;padding:22px;margin:20px 0;overflow:auto}"
             "svg{width:100%;min-width:720px}svg text{font:12px "
             "system-ui}table{border-collapse:collapse;width:100%;font-size:13px}td,th{padding:7px;"
             "border-bottom:1px solid "
             "#dae3ea;text-align:right}td:first-child,th:first-child{text-align:left}.note{color:#"
             "5b6471}"
             "@page{size:A4 landscape;margin:12mm}@media "
             "print{body{font-size:12px;margin:0}section{break-inside:avoid;padding:12px}"
             "svg{min-width:0}details{display:none}}</style><main><h1>Primary stability and "
             "uncertainty</h1><p><strong>Evidence origin: "
          << text(summary_.at("evidence_origin"))
          << ". Saved M6 exports, primary one-basis-point per-side charge and one-minute "
             "delay.</strong></p>"
             "<p>Descriptive attribution and development bounds only. Secondary variants, risk-matched "
             "ledger replays and controlled inference calibration "
             "remain open. No advancement decision is made. Missing outcomes remain unavailable; "
             "stopped accounts are neither extended nor restarted.</p>"
             "<p>Equal-weight series are research indices on all-five observed dates. Their "
             "component-average costs and exposure are not a pooled funded ledger. "
             "Shared accounts retain their own global capital and stop dates.</p>";
        coverage(h);
        intervals(h);
        heatmap(h, "instrument", "Instrument attribution | mean net bp per observed date",
                {"SPY", "QQQ", "IWM", "TLT", "GLD", "equal_weight"});
        heatmap(h, "year", "Year attribution | mean net bp per selected date", {});
        heatmap(h, "condition", "Prior-only volatility attribution | mean net bp per selected date",
                {"low", "middle", "high", "unavailable"});
        heatmap(h, "holding", "Holding-time attribution | mean net bp per selected date",
                {"cash", "up_to_15", "up_to_30", "up_to_60", "up_to_120", "up_to_390", "unavailable"});
        concentration(h);
        h << "<section><h2>All instrument and account diagnostics</h2><p>Counts, exposure, turnover, "
             "costs, selected-observation products and drawdowns are "
             "retained for every group in summary.json. The table includes unavailable groups. Products "
             "and drawdowns within a selected group do not describe "
             "a continuously funded counterfactual account. Risk exposure is at the retained mark; "
             "shared exposure is the observed intraday maximum.</p>"
             "<details><summary>Show every group</summary><table><tr><th>Series / dimension / "
             "group</th><th>Observed / selected</th><th>Active</th>"
             "<th>Mean bp/day</th><th>Exposure</th><th>Turnover / initial</th><th>Cost / "
             "initial</th><th>Selected drawdown %</th></tr>";
        for (const auto &row : summary_.at("groups")) {
            h << "<tr><td>" << text(row.at("series")) << " / " << text(row.at("dimension")) << " / "
              << text(row.at("group")) << "</td><td>" << row.at("observed_dates") << " / "
              << row.at("selected_calendar_dates") << "</td><td>" << row.at("active_dates")
              << "</td><td>"
              << numeric(row.at("statistics").is_null() ? Json(nullptr)
                                                        : row.at("statistics").at("mean"),
                         10000)
              << "</td><td>" << numeric(row.at("mean_exposure")) << "</td><td>"
              << numeric(row.at("turnover_initial_equity_units")) << "</td><td>"
              << numeric(row.at("cost_initial_equity_units")) << "</td><td>"
              << numeric(row.at("statistics").is_null()
                             ? Json(nullptr)
                             : row.at("statistics").at("selected_observation_drawdown"),
                         100)
              << "</td></tr>";
        }
        h << "</table></details></section><p class=\"note\">10,000 joint full-calendar non-circular "
             "block draws; seed 290026. "
             "Marginal percentile intervals use blocks 5/10 for intraday and 20/40 for risk. The "
             "six-endpoint family uses a single-step centered max-T, "
             "fixed original-sample Bartlett HAC studentizers and blocks 40 primary / 20 sensitivity. "
             "Bounds remain provisional until controlled calibration. "
             "At least 200 valid dates and 80% calendar coverage are required for adequate evidence. No "
             "group-level significance or trading filter is inferred.</p></main></html>";
        return h.str();
    }

  private:
    const Json &summary_;
    void coverage(std::ostringstream &h) const {
        const auto &run = summary_.at("inference").at(3);
        const double total = summary_.at("calendar_dates").get<double>();
        h << "<section><h2>Endpoint coverage</h2><svg data-chart=\"coverage\" viewBox=\"0 0 1050 270\" "
             "role=\"img\" aria-label=\"Valid dates by primary endpoint\">"
             "<line x1=\"808\" x2=\"808\" y1=\"25\" y2=\"235\" stroke=\"#ae6b1a\" stroke-dasharray=\"4 "
             "3\"/><text x=\"808\" y=\"18\" text-anchor=\"middle\">80% calendar</text>";
        std::size_t i = 0;
        for (const auto &row : run.at("endpoints")) {
            const auto n = row.at("observed_dates").get<double>();
            const auto y = 38 + 32 * i++;
            h << "<text x=\"290\" y=\"" << y + 15 << "\" text-anchor=\"end\">" << text(row.at("id"))
              << "</text>"
              << "<rect data-endpoint=\"" << text(row.at("id")) << "\" x=\"312\" y=\"" << y
              << "\" width=\"" << std::setprecision(12) << 620 * n / total << "\" height=\"21\" fill=\""
              << (row.at("adequate_coverage").get<bool>() ? "#237e9b" : "#9daebb") << "\"/>"
              << "<text x=\"940\" y=\"" << y + 15 << "\">" << static_cast<std::size_t>(n) << " / "
              << static_cast<std::size_t>(total) << "</text>";
        }
        h << "<text x=\"312\" y=\"258\">Valid original dates; cash is observed, missing is "
             "unavailable</text></svg></section>";
    }
    void intervals(std::ostringstream &h) const {
        h << "<section><h2>Paired primary uncertainty</h2><p>Dots are observed increments; blue "
             "segments are marginal 95% intervals. Orange ticks are "
             "simultaneous lower bounds (block 40); green ticks are registered material increments. An "
             "unavailable interval is labelled NA.</p>";
        for (std::size_t j = 0; j < 6; ++j) {
            const auto &primary = summary_.at("inference").at(3).at("endpoints").at(j);
            const auto &marginal = summary_.at("inference").at(j < 4 ? 1 : 3).at("endpoints").at(j);
            const double scale = j < 4 ? 10000 : j == 4 ? 100 : 1;
            const std::string units = j < 4    ? "bp/day"
                                      : j == 4 ? "annual CER percentage points"
                                               : "QLIKE loss units";
            double lo = 0, hi = primary.at("material_increment").get<double>() * scale;
            for (const auto *value : {&primary.at("estimate"), &primary.at("family_lower"),
                                      &marginal.at("marginal_lower"), &marginal.at("marginal_upper")}) {
                if (!value->is_null()) {
                    const double x = value->get<double>() * scale;
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            }
            const double pad = std::max(1e-6, (hi - lo) * .12);
            lo -= pad;
            hi += pad;
            const auto x = [&](const Json &value) {
                return 50 + 900 * (value.get<double>() * scale - lo) / (hi - lo);
            };
            h << "<h3>" << text(primary.at("id")) << "</h3><svg data-chart=\"interval\" data-endpoint=\""
              << text(primary.at("id")) << "\" data-low=\"" << lo << "\" data-high=\"" << hi
              << "\" data-scale=\"" << scale
              << "\" viewBox=\"0 0 1000 90\" role=\"img\" aria-label=\"Paired increment in " << units
              << "\">"
              << "<line x1=\"50\" x2=\"950\" y1=\"40\" y2=\"40\" stroke=\"#cbd6df\"/>";
            if (!marginal.at("marginal_lower").is_null()) {
                h << "<line data-mark=\"marginal\" x1=\"" << x(marginal.at("marginal_lower"))
                  << "\" x2=\"" << x(marginal.at("marginal_upper"))
                  << "\" y1=\"40\" y2=\"40\" stroke=\"#237e9b\" stroke-width=\"5\"/>";
            }
            if (!primary.at("estimate").is_null()) {
                h << "<circle data-mark=\"estimate\" cx=\"" << x(primary.at("estimate"))
                  << "\" cy=\"40\" r=\"5\" fill=\"#193549\"/>";
            }
            for (const auto *key : {"family_lower", "material_increment"}) {
                if (primary.at(key).is_null()) {
                    continue;
                }
                h << "<line data-mark=\"" << key << "\" x1=\"" << x(primary.at(key)) << "\" x2=\""
                  << x(primary.at(key)) << "\" y1=\"25\" y2=\"55\" stroke=\""
                  << (std::string{key} == "family_lower" ? "#ae6b1a" : "#258569")
                  << "\" stroke-width=\"3\"/>";
            }
            h << "<text x=\"50\" y=\"78\">" << std::fixed << std::setprecision(4) << lo
              << "</text><text x=\"950\" y=\"78\" text-anchor=\"end\">" << hi << ' ' << units
              << "</text></svg><p class=\"note\">Estimate " << numeric(primary.at("estimate"), scale)
              << "; marginal [" << numeric(marginal.at("marginal_lower"), scale) << ", "
              << numeric(marginal.at("marginal_upper"), scale) << "]; family lower "
              << numeric(primary.at("family_lower"), scale) << ". Coverage "
              << (primary.at("adequate_coverage").get<bool>() ? "adequate" : "input limited") << ".</p>"
              << std::defaultfloat << std::setprecision(12);
        }
        h << "</section>";
    }
    void heatmap(std::ostringstream &h, const std::string &dimension, const std::string &title,
                 std::vector<std::string> columns) const {
        std::map<std::string, std::map<std::string, const Json *>> rows;
        std::set<std::string> keys;
        for (const auto &row : summary_.at("groups")) {
            const auto id = row.at("series").get<std::string>();
            if (dimension == "instrument") {
                if (row.at("dimension").get<std::string>() == "period" &&
                    id.rfind("intraday/", 0) == 0) {
                    const auto split = id.find('/', 9);
                    rows["intraday/equal_weight/" + id.substr(split + 1)][id.substr(9, split - 9)] =
                        &row;
                }
                continue;
            }
            if (row.at("dimension").get<std::string>() != dimension ||
                id.rfind("intraday/equal_weight/", 0) != 0) {
                continue;
            }
            const auto group = row.at("group").get<std::string>();
            rows[id][group] = &row;
            keys.insert(group);
        }
        if (columns.empty()) {
            columns.assign(keys.begin(), keys.end());
        }
        h << "<section><h2>" << title
          << "</h2><p>Instrument cells retain their own observed mask; other matrices use equal-weight "
             "research indices. Cells show the mean and observed date count; grey "
             "denotes unavailable. Green denotes a nonnegative mean; red denotes a negative mean. "
             "Holding groups use total minutes invested per session, including a separate cash "
             "group.</p><svg data-chart=\"heatmap\" data-dimension=\""
          << dimension << "\" viewBox=\"0 0 1100 390\" role=\"img\" aria-label=\"" << title << "\">";
        const double width = 760 / static_cast<double>(columns.size());
        for (std::size_t j = 0; j < columns.size(); ++j) {
            h << "<text x=\"" << 320 + width * (static_cast<double>(j) + .5)
              << "\" y=\"23\" text-anchor=\"middle\">" << columns[j] << "</text>";
        }
        std::size_t i = 0;
        for (const auto &[id, cells] : rows) {
            const double y = 40 + 40 * static_cast<double>(i++);
            h << "<text x=\"310\" y=\"" << y + 23 << "\" text-anchor=\"end\">" << id.substr(22)
              << "</text>";
            for (std::size_t j = 0; j < columns.size(); ++j) {
                const auto found = cells.find(columns[j]);
                const Json *row = found == cells.end() ? nullptr : found->second;
                const Json value = row && !row->at("statistics").is_null()
                                       ? row->at("statistics").at("mean")
                                       : Json(nullptr);
                h << "<g data-series=\""
                  << (dimension == "instrument" && row ? text(row->at("series")) : id)
                  << "\" data-group=\"" << columns[j] << "\"><rect x=\""
                  << 320 + width * static_cast<double>(j) << "\" y=\"" << y << "\" width=\"" << width - 2
                  << "\" height=\"37\" fill=\""
                  << (value.is_null()            ? "#e3e9ee"
                      : value.get<double>() >= 0 ? "#d1eade"
                                                 : "#f2d9d5")
                  << "\"/><text data-value=\"" << (value.is_null() ? "null" : value.dump()) << "\" x=\""
                  << 320 + width * (static_cast<double>(j) + .5) << "\" y=\"" << y + 23
                  << "\" text-anchor=\"middle\">" << numeric(value, 10000) << " ("
                  << (row ? row->at("observed_dates").get<std::size_t>() : 0) << ")</text></g>";
            }
        }
        h << "</svg></section>";
    }
    void concentration(std::ostringstream &h) const {
        h << "<section><h2>Dependence on winning dates</h2><p>Arithmetic means after zeroing the "
             "largest positive date contributions, retaining the original observed-date denominator. "
             "This is a posthoc concentration diagnostic; positions, capital and costs are not "
             "replayed.</p><table data-chart=\"concentration\"><tr><th>Equal-weight intraday policy</th>"
             "<th>Original bp/day</th><th>Top 1 zeroed</th><th>Top 5</th><th>Top 10</th><th>Top 1% "
             "positive dates</th></tr>";
        for (const auto &row : summary_.at("groups")) {
            const auto id = row.at("series").get<std::string>();
            if (id.rfind("intraday/equal_weight/", 0) != 0 ||
                row.at("dimension").get<std::string>() != "period") {
                continue;
            }
            h << "<tr data-series=\"" << id << "\"><td>" << id.substr(22) << "</td><td>"
              << numeric(row.at("statistics").is_null() ? Json(nullptr)
                                                        : row.at("statistics").at("mean"),
                         10000)
              << "</td>";
            for (const auto &removal : row.at("concentration")) {
                h << "<td data-rule=\"" << text(removal.at("rule")) << "\" data-value=\""
                  << removal.at("mean_after_zeroing").dump() << "\">"
                  << numeric(removal.at("mean_after_zeroing"), 10000) << "</td>";
            }
            h << "</tr>";
        }
        h << "</table></section>";
    }
};
} // namespace
std::filesystem::path StabilityReport::write(const std::filesystem::path &directory,
                                             const StabilityStudy &study, const StabilityInputs &inputs,
                                             Json results, bool include_checks) {
    Json artifacts = Json::array();
    const auto save = [&](const std::string &name, const std::string &raw) {
        write_new_text_file(directory / name, raw);
        artifacts.push_back({{"path", name}, {"bytes", raw.size()}, {"sha256", sha256(raw)}});
    };
    std::ostringstream observations;
    for (const auto &series : inputs.series) {
        Json rows = Json::array();
        for (const auto &row : series.days) {
            rows.push_back(stability_observation_json(row));
        }
        observations << Json{{"id", series.id},
                             {"layer", series.layer},
                             {"symbol", series.symbol},
                             {"policy", series.policy},
                             {"days", rows}}
                            .dump()
                     << '\n';
    }
    save("observations.jsonl", observations.str());
    save("market.json", inputs.market.dump() + "\n");
    save("plan.json", study.plan().dump(2) + "\n");
    Json endpoints = Json::array();
    for (const auto &endpoint : inputs.endpoints) {
        endpoints.push_back(stability_endpoint_json(endpoint));
    }
    save("inference_inputs.json", Json{{"dates", inputs.dates}, {"endpoints", endpoints}}.dump() + "\n");
    if (include_checks) {
        auto checks = study.numerical_checks();
        checks["session_calendar_checks"] = session_calendar_checks();
        save("kernel_checks.json", checks.dump() + "\n");
    }
    results.update({{"kind", "stability_primary_diagnostics"},
                    {"schema_version", 1},
                    {"evidence_origin", inputs.synthetic ? "synthetic" : "provider"},
                    {"sources", inputs.sources},
                    {"protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256},
                    {"source_sha256", TRADING_SOURCE_SHA256},
                    {"compiler", TRADING_COMPILER_ID},
                    {"build_configuration", TRADING_BUILD_CONFIGURATION},
                    {"network_requests", 0},
                    {"reserved_strategy_inputs_opened", false},
                    {"advancement_assessed", false},
                    {"dates", inputs.dates},
                    {"artifacts", artifacts}});
    write_new_text_file(directory / "report.html", StabilityFigures{results}.html());
    results["report_sha256"] = sha256_file(directory / "report.html", 32U * 1024 * 1024);
    write_new_text_file(directory / "summary.json", results.dump(2) + "\n");
    return directory;
}
std::filesystem::path StabilityDevelopment::run(bool constructed, std::filesystem::path cost_directory,
                                                std::filesystem::path account_directory,
                                                const StabilityStudy::Progress &progress) const {
    const auto contract = ExtensionStudyContract::load(root_ / "research/extension_protocol.json");
    const auto output =
        create_unique_directory(root_ / "scratch/runs", constructed ? "stability_check" : "stability");
    std::cout << "Evidence directory: " << output.string() << std::endl;
    if (constructed) {
        const PanelReplayFixture fixture;
        const auto policy =
            PanelInputPolicy::load(root_ / "research/extension_input_policy.json", contract);
        const Json provenance{{"evidence_origin", "synthetic"},
                              {"fixture", "analytic_panel_300"},
                              {"protocol_sha256", contract.identity()}};
        CostDelayReport costs{output / "inputs", provenance, {}, CostDelayStudy{contract}};
        const auto calendar = fixture.calendar();
        const auto actions = Json::array({{{"id", "constructed_QQQ_cash"},
                                           {"symbol", "QQQ"},
                                           {"ex_date", calendar[268].date()},
                                           {"payable_date", calendar[271].date()},
                                           {"usd_per_share", 1.25}}});
        PanelAccountReport accounts{output / "inputs", provenance, {}, contract, actions};
        std::size_t count = 0;
        static_cast<void>(
            fixture.replay(policy, contract.symbols(), [&](auto days, auto sessions, bool terminal) {
                costs.append(days, sessions);
                accounts.append(days, sessions, std::vector<std::optional<MinuteBar>>(days.size()),
                                terminal);
                if (++count % 50 == 0 && progress) {
                    progress("Constructed source calendar | " + std::to_string(count) + "/300");
                }
            }));
        cost_directory = costs.finish();
        account_directory = accounts.finish();
    } else {
        if (cost_directory.empty()) {
            cost_directory = root_ / "scratch/runs/cost_delay_0";
        }
        if (account_directory.empty()) {
            account_directory = root_ / "scratch/runs/panel_accounts_1";
        }
    }
    const auto inputs = StabilityDataset{std::filesystem::absolute(cost_directory),
                                         std::filesystem::absolute(account_directory), contract}
                            .load(progress);
    const StabilityStudy study{contract};
    auto results = study.run(inputs, progress);
    if (progress) {
        progress("Writing stability plots, attribution and reproduction evidence");
    }
    return StabilityReport::write(output, study, inputs, std::move(results), constructed);
}
} // namespace trading
