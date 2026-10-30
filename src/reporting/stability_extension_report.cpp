#include "stability_extension_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "panel_replay_diagnostic.hpp"
#include "report_text.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string number(const Json &x, double scale = 1) {
    if (x.is_null())
        return "NA";
    std::ostringstream o;
    o << std::fixed << std::setprecision(4) << x.get<double>() * scale;
    return o.str();
}
std::string label(const Json &x) { return ReportText{x.get<std::string>()}.html(); }
std::string display_label(std::string text) {
    for (const auto &[from, to] :
         std::vector<std::pair<std::string, std::string>>{{"/equal_weight/", " | "},
                                                          {"daily_risk_measurement_", "risk_"},
                                                          {"daily_risk_", "risk_"},
                                                          {"observation_minutes", "grid"},
                                                          {"history_sessions", "history"},
                                                          {"hold_minutes", "hold"}}) {
        std::size_t at = 0;
        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, from.size(), to);
            at += to.size();
        }
    }
    std::replace(text.begin(), text.end(), '_', ' ');
    return ReportText{text}.html();
}
class ExtensionFigures {
  public:
    explicit ExtensionFigures(const Json &summary) : s_(summary) {}
    std::string html() const {
        std::ostringstream h;
        h << std::setprecision(12);
        h << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><title>Stability cases and funded risk "
             "matching</title>"
             "<style>body{font:16px/1.5 "
             "system-ui;color:#193549;background:#f2f5f8;margin:24px}main{max-width:1250px;margin:auto}"
             "section{background:white;padding:22px;margin:20px 0;border:1px solid "
             "#d7e0e8;border-radius:8px;overflow:auto}"
             "table{border-collapse:collapse;width:100%;font-size:13px}th,td{padding:7px;border-bottom:"
             "1px solid "
             "#dae3ea;text-align:right}th:first-child,td:first-child{text-align:left}svg{width:100%;min-"
             "width:850px}svg text{font:12px system-ui}"
             "@page{size:A4 landscape;margin:12mm}@media "
             "print{body{margin:0;font-size:12px}section{padding:12px;break-inside:avoid}svg{min-width:"
             "0}details{display:none}}</style><main><h1>Registered stability cases and funded risk "
             "matching</h1>"
             "<p><strong>Evidence origin: "
          << label(s_.at("provenance").at("evidence_origin"))
          << ". Fourteen one-factor cases; primary one-minute delay and one-basis-point per-side "
             "charge.</strong></p>"
             "<p>Risk matching uses 252 prior calendar slots of unit-account returns, a common annual "
             "volatility target capped at 10%, and multipliers no greater than one. Actual sizing, "
             "costs and cash ledgers are replayed. Missing slots stay missing; stopped accounts do not "
             "restart. "
             "The original primary research results are retained separately. These secondary findings "
             "do not select replacement parameters or authorize advancement; controlled inference "
             "calibration remains pending.</p>";
        secondary(h);
        coverage(h);
        matched(h);
        h << "<section><h2>Concentration, regimes and account ownership</h2><p>All "
          << s_.at("groups").size()
          << " year, prior-only volatility-condition, holding-time and period groups are retained in "
             "summary.json. "
             "Pooled series are equal-weight research indices on all-five observed dates, not a global "
             "funded portfolio. Shared-capital ownership/netting remains the separate accepted "
             "execution study. "
             "Intraday exposure is allocated target fraction times holding time; risk exposure is the "
             "retained closing liquid stock fraction. Neither implies observed market depth or broker "
             "liquidity.</p><details><summary>Show period diagnostics</summary>"
             "<table><tr><th>Series</th><th>Observed</th><th>Mean bp/day</th><th>Exposure</th><th>Cost "
             "/ initial</th><th>Top 5 zeroed mean bp/day</th></tr>";
        for (const auto &g : s_.at("groups")) {
            if (g.at("dimension").get<std::string>() != "period")
                continue;
            h << "<tr><td>" << label(g.at("series")) << "</td><td>" << g.at("observed_dates")
              << "</td><td>"
              << number(g.at("statistics").is_null() ? Json(nullptr) : g.at("statistics").at("mean"),
                        10000)
              << "</td><td>" << number(g.at("mean_exposure")) << "</td><td>"
              << number(g.at("cost_initial_equity_units")) << "</td><td>"
              << number(g.at("concentration").at(1).at("mean_after_zeroing"), 10000) << "</td></tr>";
        }
        h << "</table></details><p>Full-date resampling retains unavailable outcomes. The "
             "original primary QLIKE forecast diagnostic is carried unchanged in the secondary matched "
             "family; capital scaling does not alter a variance forecast. "
             "Family bounds are provisional and coverage-qualified. No 2024 or 2025 strategy inputs "
             "were opened.</p></section></main></html>";
        return h.str();
    }

  private:
    const Json &s_;
    void secondary(std::ostringstream &h) const {
        h << "<section><h2>Secondary cases | equal-weight net bp/day</h2><p>Every registered case is "
             "shown, including unavailable outcomes. Dots use primary costs and delay; candidates and "
             "controls share the same variant history and physical holding window. "
             "Different risk ledgers can stop on different dates, so these point estimates are not "
             "performance rankings.</p>";
        std::vector<const Json *> rows;
        double limit = .5;
        for (const auto &g : s_.at("groups")) {
            const auto id = g.at("series").get<std::string>();
            if (g.at("dimension").get<std::string>() == "period" && id.starts_with("secondary/") &&
                id.find("/equal_weight/") != std::string::npos) {
                rows.push_back(&g);
                if (!g.at("statistics").is_null())
                    limit =
                        std::max(limit, std::abs(g.at("statistics").at("mean").get<double>() * 10000));
            }
        }
        limit *= 1.15;
        h << "</section>";
        for (std::size_t start = 0; start < rows.size(); start += 10) {
            const auto count = std::min<std::size_t>(10, rows.size() - start);
            h << "<section><h2>Secondary cases | " << start + 1 << "-" << start + count << " of "
              << rows.size() << "</h2>";
            h << "<svg data-chart=\"secondary" << start / 10 << "\" data-limit=\"" << limit
              << "\" viewBox=\"0 0 1250 " << 70 + count * 28
              << "\" role=\"img\" aria-label=\"Secondary net return means\">";
            const auto x = [&](double value) { return 850 + 260 * value / limit; };
            h << "<line x1=\"850\" x2=\"850\" y1=\"25\" y2=\"" << 40 + count * 28
              << "\" stroke=\"#b4c1cc\"/>";
            std::size_t i = 0;
            for (std::size_t index = start; index < start + count; ++index) {
                const auto *g = rows[index];
                const auto id = g->at("series").get<std::string>();
                const double y = 40 + 28 * static_cast<double>(i++);
                h << "<text x=\"565\" y=\"" << y + 4 << "\" text-anchor=\"end\">"
                  << display_label(id.substr(10)) << "</text>";
                if (g->at("statistics").is_null())
                    h << "<text x=\"850\" y=\"" << y + 4 << "\">NA</text>";
                else
                    h << "<circle data-series=\"" << ReportText{id}.html() << "\" cx=\""
                      << x(g->at("statistics").at("mean").get<double>() * 10000) << "\" cy=\"" << y
                      << "\" r=\"4\" fill=\"#237e9b\"/>";
                h << "<text x=\"1230\" y=\"" << y + 4 << "\" text-anchor=\"end\">"
                  << g->at("observed_dates") << " dates</text>";
            }
            h << "<text x=\"590\" y=\"" << 60 + count * 28 << "\">-" << number(Json(limit))
              << " bp/day</text><text x=\"1110\" y=\"" << 60 + count * 28 << "\" text-anchor=\"end\">+"
              << number(Json(limit)) << " bp/day</text></svg></section>";
        }
    }
    void coverage(std::ostringstream &h) const {
        h << "<section><h2>Matched-pair increments and calendar coverage</h2><p>Pair intersections "
             "retain the full development calendar. The first four retain the primary common mask; risk "
             "CER uses both observed funded returns. QLIKE retains the original unscaled "
             "forecast/target mask. Bounds are provisional, without calibrated advancement.</p>"
             "<table><tr><th>Endpoint / units</th><th>Estimate</th><th>Family "
             "lower</th><th>Material</th><th>Valid / calendar</th><th>Adequate</th></tr>";
        std::size_t index = 0;
        for (const auto &e : s_.at("matched_inference").at(3).at("endpoints")) {
            const double scale = index < 4 ? 10000 : index == 4 ? 100 : 1;
            const auto unit = index < 4 ? "bp/day" : index == 4 ? "pp/year" : "QLIKE";
            ++index;
            h << "<tr data-coverage=\"" << label(e.at("id")) << "\"><td>"
              << display_label(e.at("id").get<std::string>()) << " / " << unit << "</td><td>"
              << number(e.at("estimate"), scale) << "</td><td>" << number(e.at("family_lower"), scale)
              << "</td><td>" << number(e.at("material_increment"), scale) << "</td><td>"
              << e.at("observed_dates") << " / " << e.at("calendar_dates") << "</td><td>"
              << (e.at("adequate_coverage").get<bool>() ? "yes" : "input limited") << "</td></tr>";
        }
        h << "</table></section>";
    }
    void matched(std::ostringstream &h) const {
        h << "<section><h2>Funded matched candidate and control | mean net bp/day</h2><p>Blue is the "
             "candidate; orange is its matched control. Capital, fees and stopping are replayed "
             "separately. Means can have different masks; the endpoint table uses the pair "
             "intersection.</p>";
        std::vector<const Json *> rows;
        double limit = .5;
        for (const auto &g : s_.at("groups")) {
            const auto id = g.at("series").get<std::string>();
            if (g.at("dimension").get<std::string>() == "period" && id.starts_with("matched/") &&
                id.find("/equal_weight/") != std::string::npos) {
                rows.push_back(&g);
                if (!g.at("statistics").is_null())
                    limit =
                        std::max(limit, std::abs(g.at("statistics").at("mean").get<double>() * 10000));
            }
        }
        limit *= 1.15;
        h << "<svg data-chart=\"matched\" data-limit=\"" << limit
          << "\" viewBox=\"0 0 1100 420\" role=\"img\" aria-label=\"Funded matched means\">";
        std::size_t i = 0;
        for (const auto *g : rows) {
            const auto id = g->at("series").get<std::string>();
            const double y = 40 + 32 * static_cast<double>(i++);
            h << "<text x=\"530\" y=\"" << y + 4 << "\" text-anchor=\"end\">"
              << display_label(id.substr(8)) << "</text>";
            if (g->at("statistics").is_null())
                h << "<text x=\"800\" y=\"" << y + 4 << "\">NA</text>";
            else
                h << "<circle data-series=\"" << ReportText{id}.html() << "\" cx=\""
                  << 800 + 220 * g->at("statistics").at("mean").get<double>() * 10000 / limit
                  << "\" cy=\"" << y << "\" r=\"5\" fill=\""
                  << (id.ends_with("candidate") ? "#237e9b" : "#ae6b1a") << "\"/>";
        }
        h << "<line x1=\"800\" x2=\"800\" y1=\"20\" y2=\"355\" stroke=\"#b4c1cc\"/><text x=\"580\" "
             "y=\"390\">-"
          << number(Json(limit)) << " bp/day</text><text x=\"1020\" y=\"390\" text-anchor=\"end\">+"
          << number(Json(limit)) << " bp/day</text></svg></section>";
    }
};
} // namespace
StabilityExtensionReport::StabilityExtensionReport(std::filesystem::path run_root, Json provenance,
                                                   std::filesystem::path dataset,
                                                   const ExtensionStudyContract &c, PanelInputPolicy p,
                                                   Json actions)
    : directory_(create_unique_directory(run_root, "stability_extension")),
      study_(c, std::move(p), std::move(actions)),
      summary_{{"kind", "stability_extension_study"},
               {"schema_version", 1},
               {"provenance", std::move(provenance)},
               {"input_dataset", dataset.generic_string()},
               {"protocol_sha256", c.identity()},
               {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
               {"source_sha256", TRADING_SOURCE_SHA256},
               {"compiler", TRADING_COMPILER_ID},
               {"build_configuration", TRADING_BUILD_CONFIGURATION},
               {"network_requests", 0},
               {"reserved_strategy_inputs_opened", false}} {
    for (const auto &symbol : c.symbols()) {
        auto &stream = streams_[symbol];
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(directory_ / (symbol + ".jsonl"), std::ios::binary);
    }
    write_new_text_file(directory_ / "plan.json", study_.plan().dump(2) + "\n");
}
const std::filesystem::path &StabilityExtensionReport::directory() const noexcept { return directory_; }
void StabilityExtensionReport::append(std::span<const PanelReplayDay> d,
                                      std::span<const SessionDownload> sessions,
                                      std::span<const std::optional<MinuteBar>> recovered,
                                      bool terminal) {
    for (const auto &record : study_.append(d, sessions, recovered, terminal)) {
        const auto raw = record.dump() + "\n";
        if (raw.size() > 2U * 1024 * 1024)
            throw std::invalid_argument("extension record exceeds bound");
        streams_.at(record.at("primary").at("symbol").get<std::string>()) << raw;
    }
}
std::filesystem::path StabilityExtensionReport::finish(const StabilityStudy::Progress &progress) {
    for (auto &[name, stream] : streams_) {
        static_cast<void>(name);
        stream.close();
    }
    summary_.update(study_.finish(progress));
    const auto inputs = study_.observations();
    summary_["dates"] = inputs.dates;
    std::ostringstream rows;
    for (const auto &series : inputs.series) {
        Json days = Json::array();
        for (const auto &row : series.days)
            days.push_back(stability_observation_json(row));
        rows << Json{{"id", series.id},
                     {"layer", series.layer},
                     {"symbol", series.symbol},
                     {"policy", series.policy},
                     {"days", days}}
                    .dump()
             << '\n';
    }
    write_new_text_file(directory_ / "observations.jsonl", rows.str());
    write_new_text_file(directory_ / "market.json", inputs.market.dump() + "\n");
    Json endpoints = Json::array();
    for (const auto &e : inputs.endpoints)
        endpoints.push_back(stability_endpoint_json(e));
    write_new_text_file(directory_ / "inference_inputs.json",
                        Json{{"dates", inputs.dates}, {"endpoints", endpoints}}.dump() + "\n");
    write_new_text_file(directory_ / "kernel_checks.json",
                        StabilityExtensionStudy::numerical_checks().dump(2) + "\n");
    Json artifacts = Json::array();
    for (const auto *symbol : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        const auto path = std::string{symbol} + ".jsonl";
        artifacts.push_back({{"path", path},
                             {"bytes", std::filesystem::file_size(directory_ / path)},
                             {"sha256", sha256_file(directory_ / path, 1024ULL * 1024 * 1024)}});
    }
    for (const auto *name : {"plan.json", "observations.jsonl", "market.json", "inference_inputs.json",
                             "kernel_checks.json"})
        artifacts.push_back({{"path", name},
                             {"bytes", std::filesystem::file_size(directory_ / name)},
                             {"sha256", sha256_file(directory_ / name, 512ULL * 1024 * 1024)}});
    summary_["artifacts"] = artifacts;
    write_new_text_file(directory_ / "report.html", ExtensionFigures{summary_}.html());
    summary_["report_sha256"] = sha256_file(directory_ / "report.html", 64U * 1024 * 1024);
    write_new_text_file(directory_ / "summary.json", summary_.dump(2) + "\n");
    return directory_;
}
std::filesystem::path
StabilityExtensionDevelopment::run(bool constructed, const StabilityStudy::Progress &progress) const {
    const auto c = ExtensionStudyContract::load(root_ / "research/extension_protocol.json");
    const auto p = PanelInputPolicy::load(root_ / "research/extension_input_policy.json", c);
    if (constructed) {
        const PanelReplayFixture fixture{600};
        const auto calendar = fixture.calendar();
        const Json actions = Json::array({{{"id", "constructed_QQQ_cash"},
                                           {"symbol", "QQQ"},
                                           {"ex_date", calendar[268].date()},
                                           {"payable_date", calendar[271].date()},
                                           {"usd_per_share", 1.25}}});
        StabilityExtensionReport report{root_ / "scratch/runs",
                                        {{"evidence_origin", "synthetic"},
                                         {"fixture", "analytic_panel_600"},
                                         {"protocol_sha256", c.identity()}},
                                        {},
                                        c,
                                        p,
                                        actions};
        std::cout << "Evidence directory: " << report.directory().string() << std::endl;
        std::size_t count = 0;
        static_cast<void>(fixture.replay(p, c.symbols(), [&](auto days, auto sessions, bool terminal) {
            report.append(days, sessions, std::vector<std::optional<MinuteBar>>(5), terminal);
            if (++count % 25 == 0 && progress)
                progress("Constructed secondary and funded replay | " + std::to_string(count) + "/600");
        }));
        return report.finish(progress);
    }
    const auto resolution =
        PanelSourceResolution::load(root_ / "research/extension_source_resolution.json", p);
    const PanelReplayDataset dataset{root_ / "scratch/datasets/extension_panel_2016_2025", c, p,
                                     resolution};
    Json actions = Json::array();
    for (const auto &a : dataset.accounting_actions().at("cash_distributions"))
        if (a.at("ex_date").get<std::string>() < "2024")
            actions.push_back(a);
    StabilityExtensionReport report{root_ / "scratch/runs",
                                    dataset.provenance(),
                                    root_ / "scratch/datasets/extension_panel_2016_2025",
                                    c,
                                    p,
                                    actions};
    std::cout << "Evidence directory: " << report.directory().string() << std::endl;
    const auto last = extension_calendar(CalendarRange{"2023-12-01", "2023-12-31"}).back().date();
    dataset.replay_with_sessions(
        [&](auto days, auto sessions) {
            std::vector<std::optional<MinuteBar>> recovered;
            for (const auto &day : days)
                recovered.push_back(dataset.recovered_close(day.symbol, day.date));
            report.append(days, sessions, recovered, days.front().date == last);
        },
        progress);
    return report.finish(progress);
}
} // namespace trading
