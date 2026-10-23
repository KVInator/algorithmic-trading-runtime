#include "artifact_io.hpp"
#include "recovery_diagnostic_report.hpp"

#include <filesystem>
#include <iostream>

int main() {
    try {
        std::cout << "Execution runtime | durable journal and restart diagnostic\n" << std::flush;
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch" / "runs", "recovery_diagnostic");
        const auto result = trading::execution::run_recovery_diagnostic(directory);
        trading::execution::write_recovery_diagnostic_report(directory, result);
        std::cout << "Recovery checks passed: " << result.checks.size()
                  << "\nState checkpoints: " << result.checkpoints.size()
                  << "\nPreserved incomplete tail: " << result.preserved_tail_bytes << " bytes"
                  << "\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string()
                  << "\nSynthetic recovery diagnostic completed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Recovery diagnostic failed: " << error.what() << '\n';
        return 1;
    }
}
