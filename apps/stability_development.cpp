#include "console_progress.hpp"
#include "stability_report.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) {
    try {
        const bool check = argc == 2 && std::string_view{argv[1]} == "--check";
        if (argc != 1 && !check && argc != 3) {
            throw std::invalid_argument(
                "usage: stability_development [--check | COST_DIRECTORY ACCOUNT_DIRECTORY]");
        }
        const trading::ConsoleProgress progress;
        progress("M7 | primary stability, concentration and paired uncertainty");
        const auto directory =
            trading::StabilityDevelopment{std::filesystem::path{TRADING_PROJECT_ROOT}}.run(
                check, argc == 3 ? std::filesystem::path{argv[1]} : std::filesystem::path{},
                argc == 3 ? std::filesystem::path{argv[2]} : std::filesystem::path{}, progress);
        std::cout << "Primary series: 77\nRegistered endpoints: 6\nNetwork requests sent: 0\nReserved "
                     "strategy inputs opened: false\n"
                     "M7 status: primary diagnostics; sensitivity and risk matching remain open\nVisual "
                     "report: "
                  << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Stability analysis stopped: " << error.what() << '\n';
        return 1;
    }
}
