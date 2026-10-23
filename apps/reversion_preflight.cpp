#include "console_progress.hpp"
#include "development_study_dataset.hpp"
#include "reversion_preflight_report.hpp"
#include "artifact_io.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    try {
        const bool synthetic_only = argc == 2 && std::string_view{argv[1]} == "--synthetic-only";
        if (argc > 2 || (argc == 2 && !synthetic_only)) { throw std::invalid_argument("supported option: --synthetic-only"); }
        const trading::ConsoleProgress progress;
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        progress("SPY-MR-01 | registered reversion input feasibility");
        const trading::ReversionPreflight preflight{};
        auto result = preflight.controls();
        progress("Constructed AR(1), OU mapping and completed-prefix controls passed");
        if (!synthetic_only) {
            const auto dataset = trading::DevelopmentStudyDataset{root}.load(progress);
            result.coverage = preflight.audit_coverage(dataset.sessions);
            result.study_manifest_sha256 = dataset.manifest_sha256;
        }
        const auto directory = trading::create_unique_directory(root / "scratch/runs", "reversion_preflight");
        trading::ReversionPreflightReport{result}.write(directory);
        const auto ready = std::count_if(result.coverage.begin(), result.coverage.end(),
            [](const auto& row) { return row.input_ready; });
        std::cout << "Constructed checks passed: " << result.checks.size() << "\nAudited sessions: " << result.coverage.size()
            << "\nInput-ready dates after warm-up: " << ready << "\nStudy status: registered_unscored"
            << "\nVisual report: " << (directory / "report.html").string()
            << "\nManifest: " << (directory / "summary.json").string()
            << "\nNo historical model was fitted and no strategy was scored.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Reversion preflight failed: " << error.what() << '\n';
        return 1;
    }
}
