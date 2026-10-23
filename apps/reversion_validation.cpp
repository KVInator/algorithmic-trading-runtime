#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "reversion_validation_report.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char**) {
    try {
        if (argc != 1) { throw std::invalid_argument("reversion_validation accepts no arguments"); }
        const trading::ConsoleProgress progress;
        progress("SPY-MR-01 | synthetic finite-sample and observation-noise diagnostic");
        const trading::ReversionValidationStudy study{};
        const auto cases = trading::ReversionValidationStudy::prescribed_cases();
        const auto result = study.run(cases, progress);
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "reversion_validation");
        trading::ReversionValidationReport{result}.write(directory);
        std::cout << "Synthetic cases: " << result.cases.size() << "\nTrials per case: " << result.settings.trials
            << "\nCoverage trials per case: " << result.settings.coverage_trials
            << "\nVisual report: " << (directory / "report.html").string()
            << "\nManifest: " << (directory / "summary.json").string()
            << "\nSynthetic diagnostic completed; no historical data were loaded or scored.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Reversion validation failed: " << error.what() << '\n';
        return 1;
    }
}
