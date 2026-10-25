#include "paper_market_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_market");
        const auto result = trading::PaperMarketDiagnostic{directory}.run();
        trading::PaperMarketReport{result}.write(directory);
        std::cout << "Paper market | constructed quote and calendar gates\nChecks passed: "
                  << result.checks.size() << "\nCases: " << result.cases.size()
                  << "\nNetwork requests sent: 0\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Paper market diagnostic failed: " << e.what() << '\n';
        return 1;
    }
}
