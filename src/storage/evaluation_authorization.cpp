#include "evaluation_authorization.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "intraday_momentum.hpp"
#include "intraday_reversion.hpp"
#include "json_input.hpp"
#include "panel_replay_dataset.hpp"
#include "trading_source_inventory.hpp"
#include <map>
#include <sstream>
#include <stdexcept>
namespace trading {
namespace {
using Json = nlohmann::json;
constexpr std::uintmax_t bound = 1024ULL * 1024 * 1024;
void require(bool v, const char *m) {
    if (!v)
        throw std::invalid_argument(m);
}
Json load(const std::filesystem::path &p) {
    return parse_json_document(read_text_file(p, 32U * 1024 * 1024));
}
void artifacts(const std::filesystem::path &d, const Json &s) {
    for (const auto &a : s.at("artifacts")) {
        const auto p = contained_path(d, a.at("path").get<std::string>());
        require(std::filesystem::file_size(p) == a.at("bytes").get<std::uintmax_t>() &&
                    sha256_file(p, bound) == a.at("sha256").get<std::string>(),
                "verified artifact changed");
    }
    require(sha256_file(d / "report.html", 32U * 1024 * 1024) ==
                s.at("report_sha256").get<std::string>(),
            "verified report changed");
}
Json verified(const std::filesystem::path &d, const std::string &kind) {
    const auto s = load(d / "summary.json"), r = load(d / "verification.json");
    require(s.at("kind").get<std::string>() == kind &&
                s.at("validation_protocol_sha256").get<std::string>() ==
                    TRADING_VALIDATION_PROTOCOL_SHA256,
            "stage kind/registration differs");
    require(r.at("kind").get<std::string>() == kind + "_verification" &&
                r.at("status").get<std::string>() == "verified" &&
                r.at("summary_sha256").get<std::string>() ==
                    sha256_file(d / "summary.json", 32U * 1024 * 1024) &&
                r.at("validation_protocol_sha256").get<std::string>() ==
                    TRADING_VALIDATION_PROTOCOL_SHA256,
            "independent verification receipt missing or stale");
    artifacts(d, s);
    if (s.contains("children"))
        for (const auto &c : s.at("children")) {
            const auto p = contained_path(d, c.at("path").get<std::string>());
            require(sha256_file(p / "summary.json", 32U * 1024 * 1024) ==
                        c.at("summary_sha256").get<std::string>(),
                    "verified child summary changed");
            artifacts(p, load(p / "summary.json"));
        }
    return s;
}
std::map<std::string, std::string> inventory(std::string_view raw) {
    std::istringstream stream{std::string{raw}};
    std::map<std::string, std::string> out;
    std::string line;
    while (std::getline(stream, line)) {
        const auto p = line.rfind(':');
        require(p != std::string::npos && line.size() - p - 1 == 64 &&
                    out.emplace(line.substr(0, p), line.substr(p + 1)).second,
                "invalid source inventory");
    }
    return out;
}
void calibrated_core(const std::filesystem::path &d) {
    const auto old = inventory(read_text_file(d / "source_inventory.txt")),
               current = inventory(build::source_records);
    for (const auto *name :
         {"src/research/inference_calibration.cpp", "src/research/inference_calibration.hpp",
          "src/research/stability_study.cpp", "src/research/stability_study.hpp",
          "src/research/return_statistics.cpp", "src/research/return_statistics.hpp",
          "research/validation_protocol.json"})
        require(old.at(name) == current.at(name),
                "statistical core changed after calibration; rerun and verify calibration");
}
} // namespace
PanelEvaluationAccess EvaluationAuthorization::validation(const std::filesystem::path &d,
                                                          bool constructed) const {
    static_cast<void>(ValidationProtocol::load(root_ / "research/validation_protocol.json"));
    Json s;
    if (constructed) {
        s = load(d / "summary.json");
        require(s.at("kind").get<std::string>() == "controlled_inference_calibration" &&
                    s.at("is_check").get<bool>() &&
                    s.at("validation_protocol_sha256").get<std::string>() ==
                        TRADING_VALIDATION_PROTOCOL_SHA256,
                "constructed calibration identity differs");
    } else {
        s = verified(d, "controlled_inference_calibration");
        require(!s.at("is_check").get<bool>() && s.at("controlled_calibration_completed").get<bool>() &&
                    s.at("trials_per_case").get<int>() == 1000 &&
                    s.at("joint_family_evaluations").get<int>() == 20000,
                "full registered calibration required before provider validation");
        const auto r = load(d / "verification.json");
        require(r.at("full_registered_experiment").get<bool>() &&
                    r.at("outer_moment_checks").get<int>() == 20000 &&
                    r.at("independent_full_bootstrap_replays").get<int>() == 40,
                "complete independent calibration scope required");
        calibrated_core(d);
    }
    require(!s.at("validation_strategy_inputs_opened").get<bool>() &&
                !s.at("final_strategy_inputs_opened").get<bool>(),
            "calibration opened reserved strategy inputs");
    return PanelEvaluationAccess{2024, !constructed, sha256_file(d / "summary.json", 32U * 1024 * 1024),
                                 s.at("nominal_size_excess_detected").get<bool>()};
}
std::filesystem::path EvaluationAuthorization::freeze(const std::filesystem::path &d,
                                                      const std::filesystem::path &runner) const {
    const auto s = verified(d, "panel_evaluation");
    require(s.at("evaluation_scope").at("year").get<int>() == 2024 &&
                !s.at("final_strategy_inputs_opened").get<bool>(),
            "only verified 2024 evaluation can be frozen");
    require(s.at("runner_sha256").get<std::string>() == sha256_file(runner, bound) &&
                s.at("source_sha256").get<std::string>() == TRADING_SOURCE_SHA256,
            "freeze requires the exact evaluated runner and inventory");
    const bool constructed = s.at("evidence_origin").get<std::string>() == "synthetic";
    const auto calibration = std::filesystem::path{s.at("calibration_directory").get<std::string>()};
    const auto access = validation(calibration, constructed);
    require(s.at("evaluation_scope") == access.document(), "validation authority changed before freeze");
    const auto out = create_unique_directory(
        root_ / "scratch/runs", constructed ? "evaluation_freeze_check" : "evaluation_freeze");
    const auto executable = runner.filename();
    std::filesystem::copy_file(runner, out / executable);
    std::filesystem::permissions(out / executable, std::filesystem::status(runner).permissions());
    write_new_text_file(out / "source_inventory.txt", std::string{build::source_records});
    Json f{{"kind", "panel_evaluation_freeze"},
           {"schema_version", 1},
           {"validation_protocol_sha256", TRADING_VALIDATION_PROTOCOL_SHA256},
           {"extension_protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256},
           {"study_manifest_sha256", registered_panel_study_sha256},
           {"source_sha256", TRADING_SOURCE_SHA256},
           {"evidence_origin", constructed ? "synthetic" : "provider"},
           {"validation_directory", std::filesystem::absolute(d).generic_string()},
           {"validation_summary_sha256", sha256_file(d / "summary.json", 32U * 1024 * 1024)},
           {"validation_receipt_sha256", sha256_file(d / "verification.json")},
           {"calibration_directory", std::filesystem::absolute(calibration).generic_string()},
           {"calibration_summary_sha256", access.authority()},
           {"calibration_size_excess_detected", access.calibration_size_excess()},
           {"runner", executable.generic_string()},
           {"runner_sha256", sha256_file(out / executable, bound)},
           {"source_inventory_sha256", sha256_file(out / "source_inventory.txt")},
           {"rules_selected_from_validation", false},
           {"parameters_changed", false},
           {"final_strategy_inputs_opened", false}};
    write_new_text_file(out / "freeze.json", f.dump(2) + "\n");
    return out;
}
PanelEvaluationAccess EvaluationAuthorization::final(const std::filesystem::path &d,
                                                     const std::filesystem::path &runner) const {
    static_cast<void>(ValidationProtocol::load(root_ / "research/validation_protocol.json"));
    const auto f = load(d / "freeze.json");
    require(f.at("kind").get<std::string>() == "panel_evaluation_freeze" &&
                f.at("evidence_origin").get<std::string>() == "provider" &&
                f.at("validation_protocol_sha256").get<std::string>() ==
                    TRADING_VALIDATION_PROTOCOL_SHA256 &&
                f.at("study_manifest_sha256").get<std::string>() == registered_panel_study_sha256,
            "a provider 2024 freeze is required for final access");
    require(!f.at("rules_selected_from_validation").get<bool>() &&
                !f.at("parameters_changed").get<bool>() &&
                !f.at("final_strategy_inputs_opened").get<bool>(),
            "freeze design differs");
    require(sha256_file(runner, bound) == f.at("runner_sha256").get<std::string>() &&
                sha256_file(contained_path(d, f.at("runner").get<std::string>()), bound) ==
                    f.at("runner_sha256").get<std::string>() &&
                f.at("source_sha256").get<std::string>() == TRADING_SOURCE_SHA256,
            "final run requires immutable evaluated executable");
    const auto v = std::filesystem::path{f.at("validation_directory").get<std::string>()};
    const auto s = verified(v, "panel_evaluation");
    require(sha256_file(v / "summary.json", 32U * 1024 * 1024) ==
                    f.at("validation_summary_sha256").get<std::string>() &&
                sha256_file(v / "verification.json") ==
                    f.at("validation_receipt_sha256").get<std::string>() &&
                s.at("evaluation_scope").at("year").get<int>() == 2024,
            "frozen validation changed");
    const auto access =
        validation(std::filesystem::path{f.at("calibration_directory").get<std::string>()});
    require(access.authority() == f.at("calibration_summary_sha256").get<std::string>() &&
                s.at("evidence_origin").get<std::string>() == "provider" &&
                s.at("evaluation_scope") == access.document() &&
                access.calibration_size_excess() ==
                    f.at("calibration_size_excess_detected").get<bool>() &&
                sha256_file(d / "source_inventory.txt") ==
                    f.at("source_inventory_sha256").get<std::string>() &&
                f.at("source_inventory_sha256").get<std::string>() == TRADING_SOURCE_SHA256,
            "frozen calibration, origin or inventory changed");
    return PanelEvaluationAccess{2025, true, sha256_file(d / "freeze.json"),
                                 access.calibration_size_excess()};
}
Json EvaluationAuthorization::checks(const PanelEvaluationAccess &access) const {
    Json passed = Json::array();
    const auto reject = [&](const char *name, const auto &action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, name);
        passed.push_back(name);
    };
    reject("development_cannot_open_2024", [] { PanelEvaluationAccess{}.require_price("2024-01-02"); });
    reject("validation_cannot_open_2025", [&] { access.require_price("2025-01-02"); });
    if (access.document().at("evidence_origin").get<std::string>() == "synthetic")
        reject("constructed_cannot_open_provider", [&] { access.require_origin(true); });
    const auto current = CalendarSession{"2024-01-02", "09:30", "16:00"}.bar_request("QQQ"),
               future = CalendarSession{"2025-01-02", "09:30", "16:00"}.bar_request("QQQ");
    static_cast<void>(
        IntradayMomentumStrategy{current, ResearchInputMode::registered_panel, 25, access});
    reject("default_strategy_keeps_development_limit", [&] {
        static_cast<void>(IntradayMomentumStrategy{current, ResearchInputMode::registered_panel});
    });
    reject("strategy_keeps_validation_limit", [&] {
        static_cast<void>(
            IntradayMomentumStrategy{future, ResearchInputMode::registered_panel, 25, access});
    });
    reject("residual_keeps_validation_limit", [&] {
        static_cast<void>(
            OpeningAnchorMeasurement{ResearchInputMode::registered_panel, 5, access}.decision_time(
                future));
    });
    require(access.unit_first() == "2022-12-29" && !access.scored("2023-12-29") &&
                access.scored("2024-01-02"),
            "cohort boundary differs");
    passed.push_back("exact_252_session_unit_warmup_and_fresh_evaluation_cohort");
    return passed;
}
} // namespace trading
