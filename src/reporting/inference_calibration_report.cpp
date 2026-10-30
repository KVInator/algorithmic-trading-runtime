#include "inference_calibration_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
namespace trading {
namespace {
using Json = nlohmann::json;
std::string html(const Json &summary) {
    std::ostringstream h;
    h << std::setprecision(12);
    h << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>Controlled inference "
         "calibration</title><style>body{font:15px "
         "system-ui;color:#19364c;background:#f1f5f8;margin:24px}main{max-width:1200px;margin:auto}"
         "section{background:white;padding:22px;margin:20px 0;border:1px solid "
         "#d5e0e7;border-radius:8px;overflow:auto}svg{width:100%;min-width:800px}svg text{font:13px "
         "system-ui}table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:6px;border-"
         "bottom:1px solid "
         "#dde5eb;text-align:right}td:first-child,th:first-child{text-align:left}@page{size:A4 "
         "landscape;margin:12mm}@media print{section{break-inside:avoid;padding:12px;margin:12px "
         "0}body{font-size:11px;margin:0;background:white}svg{min-width:0;height:290px}table{font-size:"
         "10px;table-layout:fixed}td,th{overflow-wrap:anywhere}th:first-child{width:23%}}</"
         "style><main><h1>Controlled simultaneous inference calibration</h1><p><strong>Evidence: "
         "constructed stationary Gaussian AR(1) families. "
      << (summary.at("is_check").get<bool>()
              ? "Short engineering check; no Monte Carlo calibration conclusion."
              : "Full registered Monte Carlo experiment.")
      << "</strong></p><p>Every trial uses the complete six-endpoint family, original fixed HAC "
         "studentizer and 10,000 joint full-calendar block draws. Known population effects sit at the "
         "registered material boundaries, including annual CER. Population and finite-sample variance "
         "are distinct; the sparse mask is fixed independently of outcomes.</p><p>Intervals below are "
         "95% Wilson Monte Carlo intervals for family boundary exceedance frequency, not return "
         "intervals. The reference is 5%. A one-sided binomial size-excess diagnostic uses Bonferroni "
         "across twenty case/block cells. Absence of detected excess is not market-wide coverage "
         "certification. Failed diagnostics are retained; the procedure is not retuned.</p>";
    for (std::size_t block : {20U, 40U}) {
        std::vector<Json> rows;
        double limit = .15;
        for (const auto &r : summary.at("cases"))
            if (r.at("block_length").get<std::size_t>() == block) {
                rows.push_back(r);
                limit = std::max(
                    limit, 1.1 * r.at("family_boundary_exceedance").at("wilson_upper").get<double>());
            }
        const auto x = [&](double p) { return 400 + 500 * p / limit; };
        h << "<section><h2>Family boundary exceedance | block " << block << "</h2><svg data-block=\""
          << block << "\" data-limit=\"" << limit
          << "\" viewBox=\"0 0 1100 410\" role=\"img\" aria-label=\"Family calibration frequencies and "
             "Monte Carlo intervals\"><line x1=\""
          << x(.05) << "\" x2=\"" << x(.05)
          << "\" y1=\"20\" y2=\"355\" stroke=\"#a65538\" stroke-dasharray=\"4 4\"/>";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto &r = rows[i];
            const auto &b = r.at("family_boundary_exceedance");
            const double y = 40 + 30 * static_cast<double>(i);
            h << "<text x=\"370\" y=\"" << y + 4 << "\" text-anchor=\"end\">"
              << ReportText{r.at("case").get<std::string>()}.html() << "</text><line data-case=\""
              << r.at("case").get<std::string>() << "\" x1=\"" << x(b.at("wilson_lower").get<double>())
              << "\" x2=\"" << x(b.at("wilson_upper").get<double>()) << "\" y1=\"" << y << "\" y2=\""
              << y << "\" stroke=\"#237e9b\"/><circle data-case=\"" << r.at("case").get<std::string>()
              << "\" cx=\"" << x(b.at("frequency").get<double>()) << "\" cy=\"" << y
              << "\" r=\"4\" fill=\"#237e9b\"/><text x=\"1080\" y=\"" << y + 4
              << "\" text-anchor=\"end\">" << b.at("events") << " / " << b.at("trials") << "</text>";
        }
        std::ostringstream axis;
        axis << std::fixed << std::setprecision(1) << 100 * limit;
        h << "<text x=\"400\" y=\"385\">0%</text><text x=\"" << x(.05)
          << "\" y=\"365\" text-anchor=\"middle\">5%</text><text x=\"900\" y=\"385\" "
             "text-anchor=\"end\">"
          << axis.str() << "%</text></svg></section>";
    }
    h << "<section><h2>Prespecified shifted-effect detection | block 40</h2><p>The alternative is a "
         "location shift from the Bonferroni 80%-power planning reference. Means and CER shift in their "
         "declared units; centering, covariance and masks stay fixed. Native checks compare this "
         "mapping with actual shifted-family replays. This is controlled effect detection, not power "
         "estimated for market strategies.</p><table><tr><th>Case</th>";
    for (const auto *name :
         {"Momentum", "Continuation", "Reversion / sign", "Reversion / drift", "Risk CER", "QLIKE"})
        h << "<th>" << name << "</th>";
    h << "</tr>";
    for (const auto &r : summary.at("cases"))
        if (r.at("block_length").get<int>() == 40) {
            h << "<tr><td>" << r.at("case").get<std::string>() << "</td>";
            for (const auto &e : r.at("endpoint_diagnostics"))
                h << "<td data-power=\"" << r.at("case").get<std::string>() << '/'
                  << e.at("id").get<std::string>() << "\">" << std::fixed << std::setprecision(2)
                  << 100 * e.at("alternative_detection").at("frequency").get<double>() << "%</td>";
            h << "</tr>";
        }
    h << "</table><p>Outer trials per case: " << summary.at("trials_per_case")
      << ". Size-excess cells: " << summary.at("size_excess_cells")
      << ". Undefined families are counted separately; unavailable bounds are not successes. The "
         "registered cases do not cover arbitrary heavy tails, heteroskedasticity or outcome-dependent "
         "missingness.</p></section></main></html>";
    return h.str();
}
} // namespace
std::filesystem::path InferenceCalibrationReport::run(bool check,
                                                      const StabilityStudy::Progress &progress) const {
    const auto contract = ExtensionStudyContract::load(root_ / "research/extension_protocol.json");
    const auto policy = ValidationProtocol::load(root_ / "research/validation_protocol.json");
    const InferenceCalibration study{contract, policy};
    const auto directory = create_unique_directory(root_ / "scratch/runs", "inference_calibration");
    std::cout << "Evidence directory: " << directory.string() << std::endl;
    const auto trials = policy.document()
                            .at("calibration")
                            .at(check ? "check_trials" : "outer_trials")
                            .get<std::size_t>();
    std::map<std::size_t, InferenceDrawSchedule> plans;
    for (std::size_t b : {20U, 40U})
        plans.try_emplace(b, 252, b, 10000, 290026);
    std::ofstream all(directory / "trials.jsonl", std::ios::binary),
        references(directory / "reference_trials.jsonl", std::ios::binary);
    all.exceptions(std::ios::badbit | std::ios::failbit);
    references.exceptions(std::ios::badbit | std::ios::failbit);
    Json aggregates = Json::array();
    std::size_t completed = 0, size_excess = 0, unavailable = 0;
    for (const auto &c : study.cases()) {
        std::map<std::size_t, std::vector<Json>> records;
        for (std::size_t trial = 0; trial < trials; ++trial) {
            const auto input = study.sample(c, trial);
            Json outputs = Json::array();
            for (std::size_t b : {20U, 40U}) {
                const auto r = FullCalendarInference{contract}.run(input, b, &plans.at(b));
                const auto row = study.trial_record(c, trial, r);
                all << row.dump() << '\n';
                records[b].push_back(row);
                outputs.push_back(r);
                ++completed;
            }
            if (trial == 0 || trial + 1 == trials) {
                Json endpoints = Json::array();
                for (const auto &e : input)
                    endpoints.push_back(stability_endpoint_json(e));
                references << Json{{"case", c.id},
                                   {"trial", trial},
                                   {"endpoints", endpoints},
                                   {"inference", outputs}}
                                  .dump()
                           << '\n';
            }
            if (progress && (completed % 100 == 0 || trial + 1 == trials))
                progress("Controlled joint families | " + std::to_string(completed) + "/" +
                         std::to_string(20 * trials) + " | " + c.id);
        }
        for (std::size_t b : {20U, 40U}) {
            auto r = study.aggregate(c, b, records.at(b));
            size_excess += r.at("size_excess_detected").get<bool>();
            unavailable += trials - r.at("available_trials").get<std::size_t>();
            aggregates.push_back(std::move(r));
        }
    }
    all.close();
    references.close();
    write_new_text_file(directory / "kernel_checks.json", study.numerical_checks().dump(2) + "\n");
    write_new_text_file(directory / "plan.json", policy.document().dump(2) + "\n");
    write_new_text_file(directory / "source_inventory.txt", std::string{build::source_records});
    Json s{{"kind", "controlled_inference_calibration"},
           {"schema_version", 1},
           {"is_check", check},
           {"controlled_calibration_completed", !check},
           {"protocol_sha256", contract.identity()},
           {"validation_protocol_sha256", policy.identity()},
           {"source_sha256", TRADING_SOURCE_SHA256},
           {"compiler", TRADING_COMPILER_ID},
           {"build_configuration", TRADING_BUILD_CONFIGURATION},
           {"cases", aggregates},
           {"trials_per_case", trials},
           {"case_count", 10},
           {"joint_family_evaluations", completed},
           {"size_excess_cells", size_excess},
           {"unavailable_families", unavailable},
           {"nominal_size_excess_detected", !check && size_excess > 0},
           {"market_coverage_certified", false},
           {"network_requests", 0},
           {"validation_strategy_inputs_opened", false},
           {"final_strategy_inputs_opened", false}};
    Json artifacts = Json::array();
    for (const auto *name : {"trials.jsonl", "reference_trials.jsonl", "kernel_checks.json", "plan.json",
                             "source_inventory.txt"})
        artifacts.push_back({{"path", name},
                             {"bytes", std::filesystem::file_size(directory / name)},
                             {"sha256", sha256_file(directory / name, 256ULL * 1024 * 1024)}});
    s["artifacts"] = artifacts;
    write_new_text_file(directory / "report.html", html(s));
    s["report_sha256"] = sha256_file(directory / "report.html");
    write_new_text_file(directory / "summary.json", s.dump(2) + "\n");
    return directory;
}
} // namespace trading
