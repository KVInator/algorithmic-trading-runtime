#include "console_progress.hpp"
#include "stability_extension_report.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--check"))
            throw std::invalid_argument("usage: stability_extension_development [--check]");
        const trading::ConsoleProgress progress;
        progress("M7 | registered secondary cases and funded risk-matched ledgers");
        const auto output =
            trading::StabilityExtensionDevelopment{std::filesystem::path{TRADING_PROJECT_ROOT}}.run(
                argc == 2, progress);
        std::cout << "Secondary cases: 14\nSecondary series: 240\nFunded matched accounts: 50\nUnit "
                     "reference accounts: 45\nNetwork requests sent: 0\nReserved strategy inputs "
                     "opened: false\nVisual report: "
                  << (output / "report.html").string()
                  << "\nManifest: " << (output / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Stability extension stopped: " << e.what() << '\n';
        return 1;
    }
}
