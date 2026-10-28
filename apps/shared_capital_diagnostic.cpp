#include "shared_capital_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        std::cout << "Shared capital | global cash, ownership, netting and durable recovery" << std::endl;
        const auto contract = trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const auto config = trading::execution::SharedCapitalConfiguration::registered(contract);
        const auto directory = trading::create_unique_directory(root / "scratch/runs", "shared_capital");
        const auto result = trading::execution::SharedCapitalDiagnostic{directory, config}.run();
        trading::SharedCapitalReport{}.write(directory, result);
        std::cout << "Checks passed: " << result.checks.size() << "\nCases: " << result.cases.size()
            << "\nNetwork requests sent: 0\nInteractive report: " << (directory / "report.html").string()
            << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << "Shared-capital diagnostic stopped: " << error.what() << '\n'; return 1; }
}
