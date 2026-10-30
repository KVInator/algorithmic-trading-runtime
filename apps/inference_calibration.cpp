#include "console_progress.hpp"
#include "inference_calibration_report.hpp"
#include <iostream>
#include <string_view>
int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--check"))
            throw std::invalid_argument("usage: inference_calibration [--check]");
        const trading::ConsoleProgress progress;
        progress("M8 | controlled six-endpoint inference calibration; no market inputs");
        const auto out =
            trading::InferenceCalibrationReport{std::filesystem::path{TRADING_PROJECT_ROOT}}.run(
                argc == 2, progress);
        std::cout << "Market strategy inputs opened: false\nNetwork requests sent: 0\nVisual report: "
                  << (out / "report.html").string() << "\nManifest: " << (out / "summary.json").string()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Inference calibration stopped: " << e.what() << '\n';
        return 1;
    }
}
