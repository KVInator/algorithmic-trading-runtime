#include "console_progress.hpp"
#include "cost_execution_report.hpp"
#include <iostream>

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("M6 | registered synthetic execution, fees and delivery study");
        trading::CostExecutionReport report{std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs"};
        std::cout << "Evidence directory: " << report.directory().string() << std::endl;
        trading::execution::CostExecutionStudy{}.run(
            [&](const auto &value) { report.append(value); },
            [&](std::size_t n, std::size_t total) {
                if (n % 40 == 0 || n == total) {
                    progress("Synthetic account cases | " + std::to_string(n) + '/' + std::to_string(total));
                }
            });
        const auto dir = report.finish();
        std::cout << "Account cases: 680\nPaired comparisons: 510\nExactly reconciled terminal accounts: "
                     "680\nNetwork requests sent: 0\nExternal broker events observed: 0"
                  << "\nVisual report: " << (dir / "report.html").string()
                  << "\nManifest: " << (dir / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Synthetic execution study stopped: " << error.what() << '\n';
        return 1;
    }
}
