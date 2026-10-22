#include "console_progress.hpp"
#include "risk_input_report.hpp"

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
        progress("SPY-RISK-01 | bounded calibration and supplemental input acquisition");
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::RiskInputDataset dataset{root / "scratch/datasets/SPY_RISK_01_inputs", root / "research/spy_distribution_reference.json"};
#ifdef _WIN32
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        const trading::ApiRequestPolicy policy{[](std::chrono::milliseconds wait) { std::this_thread::sleep_for(wait); }};
        const auto inputs = dataset.collect(
            [&](const trading::CalendarRange& range) { return policy.get([&] { return client.get_market_calendar(range); }); },
            [&](const trading::HistoricalBarRequest& request, const std::optional<std::string>& token) {
                return policy.get([&] { return client.get_historical_bars(request, token); });
            },
            [&](const trading::CorporateActionRequest& request, const std::optional<std::string>& token) {
                return policy.get([&] { return client.get_corporate_actions(request, token); });
            }, progress);
#else
        progress("Offline input verification; live acquisition is available on Windows");
        const auto inputs = dataset.load();
#endif
        progress("Verifying frozen development inputs and supplemental coverage");
        const auto preflight = trading::RiskPreflight{root}.run(progress);
        const auto directory = trading::save_risk_input_report(inputs, preflight, root / "scratch/runs");
        std::cout << "Price input windows: " << inputs.prices.size() << '\n'
                  << "Retained cash-distribution records: " << inputs.actions.distributions().size() << '\n'
                  << "Study status: blocked_before_registration\n"
                  << "Visual report: " << (directory / "report.html").string() << '\n'
                  << "Manifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Risk input acquisition failed: " << error.what() << '\n';
        return 1;
    }
}
