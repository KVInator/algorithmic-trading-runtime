#include "paper_transport_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_control");
        const auto result = trading::PaperTransportDiagnostic{directory}.run_control();
        trading::PaperTransportReport{result}.write(directory);
        std::cout << "Paper control | durable deadline, stop and stream health\nChecks passed: "
                  << result.checks.size() << "\nCases: " << result.cases.size()
                  << "\nNetwork requests sent: 0\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Paper control diagnostic failed: " << e.what() << '\n';
        return 1;
    }
}
