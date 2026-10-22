#include "console_progress.hpp"
#include "risk_preflight_report.hpp"

#ifdef _WIN32
#include "alpaca_console_credentials.hpp"
#include "api_request_policy.hpp"
#include <thread>
#endif

#include <exception>
#include <iostream>

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("SPY-RISK-01 | data and synthetic accounting preflight starting");
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto result = trading::RiskPreflight{root}.run(progress);
        std::vector<trading::CorporateActionProbe> probes;
        std::string access = "Not probed on this platform; endpoint access remains unverified.";
#ifdef _WIN32
        try {
            const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
            const trading::ApiRequestPolicy policy{[](std::chrono::milliseconds wait) { std::this_thread::sleep_for(wait); }};
            for (int year = 2021; year <= 2023; ++year) {
                progress("Read-only corporate-action access probe | process-date year " + std::to_string(year));
                const auto response = policy.get([&] { return client.get_corporate_action_probe(year); });
                probes.push_back(trading::inspect_corporate_action_probe(year, response));
            }
            access = "Three bounded read-only access requests completed; inspect HTTP status and retained responses. No action ledger is certified.";
        } catch (const std::exception& error) {
            access = "Corporate-action access probe incomplete: " + std::string{error.what()};
            progress(access);
        }
#endif
        progress("Writing coverage chart, constructed account ledger and prerequisite inventory");
        const auto directory = trading::save_risk_preflight_report(result, probes, access, root / "scratch/runs");
        std::cout << "Calendar sessions: " << result.coverage.size() << '\n'
                  << "Constructed accounting identities passed: " << result.accounting.checks.size() << '\n'
                  << "Study status: blocked_before_registration\n"
                  << "Visual report: " << (directory / "report.html").string() << '\n'
                  << "Manifest: " << (directory / "summary.json").string() << '\n'
                  << "Audit completed; no strategy was scored and frozen inputs were not modified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Risk preflight failed: " << error.what() << '\n';
        return 1;
    }
}
