#include "process_broker_diagnostic.hpp"
#include "process_broker_report.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 1) {
            throw std::invalid_argument("process broker diagnostic takes no arguments");
        }
        auto executable = std::filesystem::absolute(argv[0]).parent_path() / "synthetic_broker_peer";
#ifdef _WIN32
        executable += ".exe";
#endif
        std::cout << "Synthetic broker | independent native process, lifecycle and recovery" << std::endl;
        std::cout << "Malformed-frame fixtures intentionally stop two child peers." << std::endl;
        const auto result =
            trading::execution::ProcessBrokerDiagnostic{
                std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", executable}
                .run();
        trading::ProcessBrokerReport{}.write(result);
        std::cout << "Checks passed: " << result.checks.size()
                  << "\nScenario families: " << result.cases.size()
                  << "\nNative child processes: " << result.process_ids.size()
                  << "\nNetwork requests sent: 0\nInteractive report: "
                  << (result.directory / "report.html").string()
                  << "\nManifest: " << (result.directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Process broker diagnostic stopped: " << error.what() << '\n';
        return 1;
    }
}
