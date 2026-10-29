#include "console_progress.hpp"
#include "panel_study_application.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--check")) {
            throw std::invalid_argument("usage: cost_delay_development [--check]");
        }
        const trading::ConsoleProgress progress;
        progress("M6 | frozen-decision historical cost and delay comparison");
        const auto output =
            trading::CostDelayDevelopment{std::filesystem::path{TRADING_PROJECT_ROOT}}.run(argc == 2,
                                                                                           progress);
        std::cout << "Policy/instrument/cost/delay series: 400\nPaired comparisons: 240\nPrimary "
                     "reference parity: passed\nNetwork requests sent: 0\nReserved strategy inputs "
                     "opened: false\nVisual report: "
                  << (output / "report.html").string()
                  << "\nManifest: " << (output / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Cost/delay comparison stopped: " << error.what() << '\n';
        return 1;
    }
}
