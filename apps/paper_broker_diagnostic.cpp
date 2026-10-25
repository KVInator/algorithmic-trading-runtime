#include "paper_broker_diagnostic.hpp"
#include "paper_transport_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_broker");
        const auto result = trading::PaperBrokerDiagnostic{directory}.run();
        trading::PaperTransportReport{result}.write(directory);
        std::cout << "Paper broker | deterministic synthetic lifecycle and recovery\nChecks passed: "
                  << result.checks.size() << "\nCases: " << result.cases.size()
                  << "\nNetwork requests sent: 0\n"
                  << "Interrupted and uninterrupted final ledgers: identical\nVisual report: "
                  << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Simulated paper broker failed: " << e.what() << '\n';
        return 1;
    }
}
