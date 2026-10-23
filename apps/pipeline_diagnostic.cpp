#include "artifact_io.hpp"
#include "pipeline_diagnostic_report.hpp"

#include <filesystem>
#include <iostream>

int main() {
    try {
        std::cout << "Execution runtime | offline strategy, broker and recovery pipeline\n" << std::flush;
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch" / "runs", "pipeline_diagnostic");
        const auto result = trading::execution::PipelineDiagnostic{directory}.run();
        trading::execution::PipelineDiagnosticReport{result}.write(directory);
        std::cout << "Pipeline checks passed: " << result.checks.size() << "\nCases: " << result.cases.size()
                  << "\nInterrupted and uninterrupted broker paths: identical"
                  << "\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string()
                  << "\nSynthetic pipeline diagnostic completed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Pipeline diagnostic failed: " << error.what() << '\n';
        return 1;
    }
}
