#include "artifact_io.hpp"
#include "execution_diagnostic_report.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

int main() {
    try {
        std::cout << "Execution runtime | deterministic synthetic broker-message diagnostic\n" << std::flush;
        const auto results = trading::execution::ExecutionDiagnostic{}.run();
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch" / "runs", "execution_diagnostic");
        trading::execution::ExecutionDiagnosticReport{results}.write(directory);
        std::size_t transitions = 0;
        for (const auto& result : results) { transitions += result.trace.size(); }
        std::cout << "Scenarios passed: " << results.size() << "\nChecked transitions: " << transitions
                  << "\nReplay: identical traces on both passes"
                  << "\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string()
                  << "\nSynthetic execution diagnostic completed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Execution diagnostic failed: " << error.what() << '\n';
        return 1;
    }
}
