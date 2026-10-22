#include "alpaca_console_credentials.hpp"
#include "api_request_policy.hpp"
#include "session_gap_probe.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    try {
        if (argc > 2) { throw std::invalid_argument("expected at most one saved-session directory"); }
        std::cout << "Historical SIP gap probe | diagnostic evidence only\n";
        std::string source;
        if (argc == 2) { source = argv[1]; }
        else {
            std::cout << "Paste the saved incomplete session directory, then press Enter:\n";
            if (!std::getline(std::cin, source)) { throw std::runtime_error("saved-session directory was not supplied"); }
        }
        if (source.size() >= 2 && source.front() == '"' && source.back() == '"') {
            source = source.substr(1, source.size() - 2);
        }
        if (source.empty()) { throw std::invalid_argument("saved-session directory must not be empty"); }
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        const trading::ApiRequestPolicy policy{[](std::chrono::milliseconds wait) { std::this_thread::sleep_for(wait); }};
        const auto result = trading::SessionGapProbe{source}.run(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch" / "runs",
            [&](const auto& request, const auto& token) {
                return policy.get([&] { return client.get_historical_bars(request, token); });
            }, [](const std::string& message) { std::cout << message << std::endl; });
        std::cout << "Probe windows completed: " << result.probe_windows << '\n'
                  << "Originally missing minutes: " << result.missing_minutes << '\n'
                  << "Missing minutes returned by probes: " << result.candidate_minutes_found << '\n'
                  << "Conflicting minute values: " << result.conflicting_minutes << '\n'
                  << "Saved report: " << result.directory.string() << '\n'
                  << "Dataset checkpoints and manifests were not modified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Gap probe failed: " << error.what() << '\n';
        return 1;
    }
}
