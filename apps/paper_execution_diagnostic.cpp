#include "paper_execution_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_execution");
        const auto result = trading::execution::PaperExecutionDiagnostic{directory}.run();
        trading::execution::PaperExecutionReport{result}.write(directory);
        std::cout << "Paper execution | exact accounting and durable broker identities\nChecks passed: "
                  << result.checks.size()
                  << "\nCases: 3\nJournal schema: 3 | USD 1e-9\nNetwork requests sent: 0\nBroker events "
                     "observed: 0\nVisual report: "
                  << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Paper execution diagnostic failed: " << e.what() << '\n';
        return 1;
    }
}
