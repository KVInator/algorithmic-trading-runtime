#include "console_progress.hpp"
#include "panel_study_application.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--check")) {
            throw std::invalid_argument("usage: panel_account_development [--check]");
        }
        const trading::ConsoleProgress progress;
        progress("M6 | continuous risk ledgers and shared-capital economic references");
        const auto output =
            trading::PanelAccountDevelopment{std::filesystem::path{TRADING_PROJECT_ROOT}}.run(argc == 2,
                                                                                              progress);
        std::cout << "Risk account cases: 200\nShared-capital cases: 50\nNetwork requests sent: "
                     "0\nReserved strategy inputs opened: false\nVisual report: "
                  << (output / "report.html").string()
                  << "\nManifest: " << (output / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Account comparison stopped: " << error.what() << '\n';
        return 1;
    }
}
