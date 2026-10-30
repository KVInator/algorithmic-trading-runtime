#include "panel_evaluation.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "cost_delay_report.hpp"
#include "json_input.hpp"
#include "panel_account_report.hpp"
#include "panel_replay_diagnostic.hpp"
#include "report_text.hpp"
#include "stability_dataset.hpp"
#include "stability_extension_report.hpp"
#include "stability_report.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace trading {
namespace {
using Json = nlohmann::json;
std::string printed(const Json &value) {
    if (value.is_null())
        return "unavailable";
    std::ostringstream out;
    out << std::scientific << std::setprecision(4) << value.get<double>();
    return out.str();
}
std::string figure(const Json &s, const Json &primary) {
    std::ostringstream h;
    h << std::setprecision(12);
    auto status = s.at("assessment").get<std::string>();
    std::replace(status.begin(), status.end(), '_', ' ');
    h << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>Panel "
         "evaluation</title><style>body{font:16px "
         "system-ui;color:#17344a;background:#eff3f6;margin:24px}main{max-width:1150px;margin:auto}"
         "section{background:white;padding:24px;margin:20px 0;border:1px solid "
         "#d5dfe6;border-radius:8px}svg{width:100%}svg text{font:14px "
         "system-ui}table{border-collapse:collapse;width:100%;table-layout:fixed}td,th{padding:8px;text-"
         "align:left;border-bottom:1px solid "
         "#d5dfe6;overflow-wrap:anywhere}th:first-child{width:40%}a{color:#146e92}@page{size:A4 "
         "landscape;margin:12mm}@media "
         "print{section{break-inside:avoid}body{font-size:12px;margin:0}}</style><main><h1>"
      << s.at("evaluation_scope").at("year")
      << " | Integrated panel evaluation</h1><p><strong>Evidence: "
      << s.at("evidence_origin").get<std::string>() << ". " << ReportText{status}.html()
      << ".</strong></p><p>Five instruments; all primary rules and fourteen registered secondary cases. "
         "Fresh evaluation capital; prior-only histories remain continuous. Unit references begin 252 "
         "exchange sessions before the evaluation boundary. Stopped unit, matched and shared ledgers "
         "never restart.</p><p>Costs are registered research charges and delayed raw-open references, "
         "not observed fills. Provider evaluation is retrospective with prior-exposure qualifications. "
         "No best variant is selected. "
      << (s.at("evaluation_scope").at("year").get<int>() == 2024
              ? "2025 strategy prices were not opened."
              : "This explicit final run uses the previously frozen executable.")
      << "</p><section><h2>Primary endpoint coverage</h2><p>Observed paired dates out of the complete "
         "evaluation calendar; adequacy requires at least 200 and 80%. Unavailable endpoints retain "
         "their place in the family.</p><svg viewBox=\"0 0 1100 330\" role=\"img\" aria-label=\"Primary "
         "paired coverage\">";
    const auto &endpoints = primary.at("inference").at(3).at("endpoints");
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
        const auto &e = endpoints[i];
        const double n = e.at("observed_dates").get<double>(),
                     total = e.at("calendar_dates").get<double>(), y = 25 + 42 * static_cast<double>(i);
        h << "<text x=\"340\" y=\"" << y + 16 << "\" text-anchor=\"end\">"
          << e.at("id").get<std::string>() << "</text><rect x=\"365\" y=\"" << y
          << "\" width=\"500\" height=\"24\" fill=\"#e3ebf0\"/><rect data-endpoint=\""
          << e.at("id").get<std::string>() << "\" data-observed=\"" << e.at("observed_dates")
          << "\" x=\"365\" y=\"" << y << "\" width=\"" << 500 * n / total
          << "\" height=\"24\" fill=\"#237f9b\"/><text x=\"900\" y=\"" << y + 17 << "\">"
          << e.at("observed_dates") << " / " << e.at("calendar_dates") << "</text>";
    }
    h << "</svg></section><section><h2>Inference qualification</h2><p>"
      << (s.at("evidence_origin").get<std::string>() == "synthetic"
              ? "Short engineering calibration only; no Monte Carlo or market inference conclusion."
          : s.at("calibration_size_excess_detected").get<bool>()
              ? "Controlled size-excess diagnostic detected distortion. Bounds remain provisional "
                "descriptive output; supported inference claims are withheld."
              : "Controlled calibration completed under declared Gaussian AR(1) cases. This is not "
                "market-wide coverage certification; bounds remain conditional on resampling "
                "assumptions and complete-family coverage.")
      << "</p><table><tr><th>Endpoint</th><th>Estimate</th><th>Block-40 family lower</th><th>Coverage "
         "adequate</th></tr>";
    for (const auto &e : endpoints)
        h << "<tr><td>" << e.at("id").get<std::string>() << "</td><td>" << printed(e.at("estimate"))
          << "</td><td>" << printed(e.at("family_lower")) << "</td><td>"
          << (e.at("adequate_coverage").get<bool>() ? "yes" : "no") << "</td></tr>";
    h << "</table><p>Mean increments use return units per observed date; CER uses annual return units; "
         "QLIKE uses loss units. A relative increment alone is not an own-account profitability or "
         "deployment result.</p></section><section><h2>Linked evidence</h2><ul>";
    for (const auto &c : s.at("children"))
        h << "<li><a href=\"" << c.at("path").get<std::string>() << "/report.html\">"
          << c.at("name").get<std::string>() << "</a></li>";
    h << "</ul><p>The child reports retain concentration, causal-condition attribution, exposure, "
         "turnover, drawdown and paired full-calendar uncertainty alongside "
         "returns.</p></section></main></html>";
    return h.str();
}
} // namespace
std::filesystem::path PanelEvaluation::run(const PanelEvaluationAccess &access,
                                           const std::filesystem::path &calibration,
                                           const std::filesystem::path &runner,
                                           const StabilityStudy::Progress &progress,
                                           std::filesystem::path final_freeze) const {
    if (access.development())
        throw std::invalid_argument("evaluation requires explicit stage capability");
    if (access.year() == 2025 &&
        (final_freeze.empty() || sha256_file(final_freeze / "freeze.json") != access.authority()))
        throw std::invalid_argument("final evaluation requires bound freeze record");
    const auto contract = ExtensionStudyContract::load(root_ / "research/extension_protocol.json");
    const auto policy = PanelInputPolicy::load(root_ / "research/extension_input_policy.json", contract);
    const auto resolution =
        PanelSourceResolution::load(root_ / "research/extension_source_resolution.json", policy);
    const bool synthetic = access.document().at("evidence_origin").get<std::string>() == "synthetic";
    if (synthetic && access.year() != 2024)
        throw std::invalid_argument("constructed final data are outside the validation fixture");
    const auto out =
        create_unique_directory(root_ / "scratch/runs", synthetic               ? "validation_check"
                                                        : access.year() == 2024 ? "validation"
                                                                                : "final_evaluation");
    std::cout << "Evidence directory: " << out.string() << std::endl;
    const auto input = root_ / "scratch/datasets/extension_panel_2016_2025";
    const auto calendar =
        extension_calendar_years(std::stoi(access.first_input().substr(0, 4)), access.year());
    std::unique_ptr<PanelReplayDataset> dataset;
    Json provenance, actions = Json::array();
    if (synthetic) {
        provenance = {{"evidence_origin", "synthetic"},
                      {"fixture", "analytic_evaluation_2022_2024"},
                      {"protocol_sha256", contract.identity()}};
        actions.push_back({{"id", "constructed_QQQ_cash"},
                           {"symbol", "QQQ"},
                           {"ex_date", calendar.at(268).date()},
                           {"payable_date", calendar.at(271).date()},
                           {"usd_per_share", 1.25}});
        actions.push_back({{"id", "constructed_QQQ_evaluation_cash"},
                           {"symbol", "QQQ"},
                           {"ex_date", calendar.at(600).date()},
                           {"payable_date", calendar.at(603).date()},
                           {"usd_per_share", .75}});
    } else {
        dataset = std::make_unique<PanelReplayDataset>(
            input, contract, policy, resolution, PanelOrigin::provider,
            std::string{registered_panel_study_sha256}, access);
        provenance = dataset->provenance();
        for (const auto &a : dataset->accounting_actions().at("cash_distributions"))
            if (a.at("ex_date").get<std::string>() <= access.last_input())
                actions.push_back(a);
    }
    CostDelayReport costs{out, provenance, synthetic ? std::filesystem::path{} : input,
                          CostDelayStudy{contract, access}, access};
    PanelAccountReport accounts{out,      provenance, synthetic ? std::filesystem::path{} : input,
                                contract, actions,    access};
    StabilityExtensionReport extensions{
        out, provenance, synthetic ? std::filesystem::path{} : input, contract, policy, actions, access};
    std::size_t count = 0;
    const auto consume = [&](auto days, auto sessions) {
        std::vector<std::optional<MinuteBar>> recovered;
        for (const auto &d : days)
            recovered.push_back(synthetic ? std::nullopt : dataset->recovered_close(d.symbol, d.date));
        const bool terminal = days.front().date == calendar.back().date();
        costs.append(days, sessions);
        accounts.append(days, sessions, recovered, terminal);
        extensions.append(days, sessions, recovered, terminal);
        ++count;
        if (synthetic && progress && count % 25 == 0)
            progress("Constructed validation calendar | " + std::to_string(count) + "/" +
                     std::to_string(calendar.size()));
    };
    if (synthetic) {
        std::map<std::string, PanelReplayState> states;
        for (const auto &symbol : contract.symbols())
            states.try_emplace(symbol, symbol, policy, PanelReplayParameters{}, access);
        for (std::size_t i = 0; i < calendar.size(); ++i) {
            std::vector<PanelReplayDay> days;
            std::vector<SessionDownload> sessions;
            for (const auto &symbol : contract.symbols()) {
                auto bars = PanelReplayFixture::bars(calendar[i], symbol, i);
                const auto request = calendar[i].bar_request(symbol);
                double dividend = 0;
                for (const auto &a : actions)
                    if (a.at("symbol").get<std::string>() == symbol &&
                        a.at("ex_date").get<std::string>() == calendar[i].date())
                        dividend += a.at("usd_per_share").get<double>();
                days.push_back(states.at(symbol).advance(request, bars, dividend));
                const auto audit = audit_session(request, bars);
                sessions.push_back({request, std::move(bars), {}, audit, {}, {}});
            }
            const bool common = std::all_of(days.begin(), days.end(),
                                            [](const auto &d) { return d.availability.operational; });
            for (auto &d : days)
                d.common_operational = common;
            consume(std::move(days), std::move(sessions));
        }
    } else
        dataset->replay_with_sessions(consume, progress);
    if (count != calendar.size())
        throw std::invalid_argument("evaluation source calendar incomplete");
    const auto cost_dir = costs.finish(), account_dir = accounts.finish(),
               extension_dir = extensions.finish(progress);
    const auto inputs = StabilityDataset{cost_dir, account_dir, contract, access}.load(progress);
    const StabilityStudy study{contract};
    const auto primary = study.run(inputs, progress);
    const auto primary_dir = create_unique_directory(out, "primary");
    static_cast<void>(StabilityReport::write(primary_dir, study, inputs, primary, false, access));
    Json children = Json::array();
    for (const auto &[name, dir] : std::vector<std::pair<std::string, std::filesystem::path>>{
             {"Cost and delay", cost_dir},
             {"Continuous and shared accounts", account_dir},
             {"Secondary and matched accounts", extension_dir},
             {"Primary stability", primary_dir}})
        children.push_back({{"name", name},
                            {"path", dir.filename().generic_string()},
                            {"summary_sha256", sha256_file(dir / "summary.json", 32U * 1024 * 1024)}});
    const auto matched_summary =
        parse_json_document(read_text_file(extension_dir / "summary.json", 32U * 1024 * 1024));
    const auto &family = primary.at("inference").at(3);
    const auto &matched = matched_summary.at("matched_inference").at(3);
    const bool full =
        family.at("family_available").get<bool>() && family.at("all_endpoints_adequate").get<bool>();
    Json s{{"kind", "panel_evaluation"},
           {"schema_version", 1},
           {"evaluation_scope", access.document()},
           {"validation_protocol_sha256", TRADING_VALIDATION_PROTOCOL_SHA256},
           {"protocol_sha256", contract.identity()},
           {"provenance", provenance},
           {"evidence_origin", synthetic ? "synthetic" : "provider"},
           {"calibration_directory", std::filesystem::absolute(calibration).generic_string()},
           {"calibration_size_excess_detected", access.calibration_size_excess()},
           {"source_sha256", TRADING_SOURCE_SHA256},
           {"compiler", TRADING_COMPILER_ID},
           {"build_configuration", TRADING_BUILD_CONFIGURATION},
           {"runner_sha256", sha256_file(runner, 1024ULL * 1024 * 1024)},
           {"children", children},
           {"primary_family_available", family.at("family_available")},
           {"primary_coverage_adequate", family.at("all_endpoints_adequate")},
           {"matched_family_available", matched.at("family_available")},
           {"matched_coverage_adequate", matched.at("all_endpoints_adequate")},
           {"source_calendar_dates", count},
           {"evaluation_dates", inputs.dates},
           {"primary_series", 77},
           {"secondary_and_matched_series", 345},
           {"registered_variants", 14},
           {"network_requests", 0},
           {"external_broker_events", 0},
           {"validation_strategy_inputs_opened", !synthetic && access.year() == 2024},
           {"final_strategy_inputs_opened", !synthetic && access.year() == 2025},
           {"rules_selected_from_validation", false},
           {"parameters_changed", false},
           {"strategy_advancement_authorized", false},
           {"assessment",
            synthetic                          ? "constructed_pipeline_check_no_market_claim"
            : access.calibration_size_excess() ? "descriptive_only_controlled_size_excess"
            : !full                            ? "input_limited_complete_family_unavailable"
                    : "registered_bounds_with_assumption_qualifications_no_automatic_advancement"}};
    if (access.year() == 2025)
        s["final_freeze_directory"] = std::filesystem::absolute(final_freeze).generic_string();
    write_new_text_file(
        out / "plan.json",
        ValidationProtocol::load(root_ / "research/validation_protocol.json").document().dump(2) + "\n");
    write_new_text_file(out / "source_inventory.txt", std::string{build::source_records});
    write_new_text_file(
        out / "access_checks.json",
        access.year() == 2024 ? EvaluationAuthorization{root_}.checks(access).dump(2) + "\n" : "[]\n");
    Json artifacts = Json::array();
    for (const auto *name : {"plan.json", "source_inventory.txt", "access_checks.json"})
        artifacts.push_back({{"path", name},
                             {"bytes", std::filesystem::file_size(out / name)},
                             {"sha256", sha256_file(out / name)}});
    s["artifacts"] = artifacts;
    write_new_text_file(out / "report.html", figure(s, primary));
    s["report_sha256"] = sha256_file(out / "report.html");
    write_new_text_file(out / "summary.json", s.dump(2) + "\n");
    return out;
}
} // namespace trading
