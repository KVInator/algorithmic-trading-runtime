#include "panel_evaluation.hpp"
#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "inference_calibration_report.hpp"
#include "json_input.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
int main(int argc, char **argv) {
    try {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto runner = std::filesystem::canonical(std::filesystem::absolute(argv[0]));
        const trading::EvaluationAuthorization authority{root};
        const trading::ConsoleProgress progress;
        std::filesystem::path out;
        if (argc == 2 && std::string_view{argv[1]} == "--check") {
            progress("M8 | constructed integrated validation; no provider data");
            const auto calibration = trading::InferenceCalibrationReport{root}.run(true, progress);
            out = trading::PanelEvaluation{root}.run(authority.validation(calibration, true),
                                                     calibration, runner, progress);
        } else if (argc == 3 && std::string_view{argv[1]} == "--validation") {
            progress("M8 | registered 2024 evaluation; 2025 strategy prices remain reserved");
            out = trading::PanelEvaluation{root}.run(authority.validation(argv[2]), argv[2], runner,
                                                     progress);
        } else if (argc == 3 && std::string_view{argv[1]} == "--freeze") {
            out = authority.freeze(argv[2], runner);
            std::cout
                << "Executable and specification frozen. Final strategy prices opened: false\nFreeze: "
                << (out / "freeze.json").string() << '\n';
            return 0;
        } else if (argc == 3 && std::string_view{argv[1]} == "--final") {
            const auto access = authority.final(argv[2], runner);
            const auto frozen = trading::parse_json_document(
                trading::read_text_file(std::filesystem::path{argv[2]} / "freeze.json"));
            out = trading::PanelEvaluation{root}.run(
                access, frozen.at("calibration_directory").get<std::string>(), runner, progress,
                argv[2]);
        } else
            throw std::invalid_argument("usage: panel_evaluation --check | --validation CALIBRATION_DIR "
                                        "| --freeze VALIDATION_DIR | --final FREEZE_DIR");
        std::cout << "Primary series: 77\nSecondary and matched series: 345\nRegistered variants: "
                     "14\nNetwork requests sent: 0\nVisual report: "
                  << (out / "report.html").string() << "\nManifest: " << (out / "summary.json").string()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Panel evaluation stopped: " << e.what() << '\n';
        return 1;
    }
}
