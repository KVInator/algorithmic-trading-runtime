#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "development_study_dataset.hpp"
#include "reversion_study_report.hpp"

#include <iostream>

int main() {
    try {
        const trading::ConsoleProgress progress;
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        progress("SPY-MR-01 | registered prior-only development comparison starting");
        trading::ReversionStudy::verify_registration(root);
        const auto dataset = trading::DevelopmentStudyDataset{root}.load(progress);
        const auto result = trading::ReversionStudy{}.run_frozen(dataset, root, progress);
        progress("Writing frozen decisions, forty account cases, paired uncertainty and gates");
        const auto directory =
            trading::create_unique_directory(root / "scratch/runs", "SPY_MR_01_development_2022-2023");
        trading::ReversionStudyReport{result}.write(directory, dataset.manifest_sha256);
        const auto& s = result.statistics[2].overall;
        std::cout << "Scored dates: " << result.days.size()
                  << "\nComparison account cases: 40\nPrimary active dates: " << s.returns.active_days
                  << "\nCommon-mask forecast dates: " << result.metrics[86].available
                  << "\nRegistered assessment: " << result.status
                  << "\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string()
                  << "\nOffline development comparison completed; frozen inputs were not modified.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Reversion development stopped: " << e.what() << '\n';
        return 1;
    }
}
