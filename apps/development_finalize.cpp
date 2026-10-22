#include "development_study_dataset.hpp"
#include "console_progress.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("SPY-IM-01 | offline finalization starting");
        const auto dataset = trading::DevelopmentStudyDataset{TRADING_PROJECT_ROOT}.finalize(progress);
        std::cout << "SPY-IM-01 | protocol revision 2 | offline finalization\n"
                  << "Scored sessions: " << dataset.sessions.size() << '\n'
                  << "Validated scored bars: " << dataset.sessions.size() * 390 << '\n'
                  << "Excluded sessions: " << dataset.exclusions.size() << '\n';
        for (const auto& exclusion : dataset.exclusions) { std::cout << exclusion.date << " | " << exclusion.reason << '\n'; }
        std::cout << "March manifest SHA-256: " << dataset.march_manifest_sha256 << '\n'
                  << "Study manifest SHA-256: " << dataset.manifest_sha256 << '\n'
                  << "Data policy SHA-256: " << dataset.data_policy_sha256 << '\n'
                  << "Saved study manifest: " << (dataset.directory / dataset.manifest_file).string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Development finalization failed: " << error.what() << '\n';
        return 1;
    }
}
