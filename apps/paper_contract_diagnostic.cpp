#include "paper_contract_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_contract");
        const auto result = trading::PaperContractDiagnostic{}.run();
        trading::PaperContractReport{result}.write(directory);
        std::cout << "Alpaca paper | constructed message and precision contract\nChecks passed: "
                  << result.checks.size() << "\nCases: " << result.cases.size()
                  << "\nNetwork requests sent: 0\nBroker events observed: 0\nVisual report: "
                  << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string()
                  << "\nContract diagnostic completed; execution journal schemas were not changed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Paper contract diagnostic failed: " << error.what() << '\n';
        return 1;
    }
}
