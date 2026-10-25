#include "project_demo.hpp"
#include <iostream>

int main() {
    try {
        std::cout << "Project demo | causal strategies, synthetic broker and durable recovery" << std::endl;
        const auto result =
            trading::ProjectDemo{}.run(std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs");
        std::cout << "Checks passed: " << result.checks << "\nCases: " << result.cases
                  << "\nNetwork requests sent: 0\nVisual report: "
                  << (result.directory / "report.html").string()
                  << "\nManifest: " << (result.directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Project demo failed: " << error.what() << '\n';
        return 1;
    }
}
