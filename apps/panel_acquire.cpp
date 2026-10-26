#include "console_progress.hpp"
#include "extension_contract.hpp"
#include "historical_panel.hpp"
#include "historical_panel_report.hpp"
#include <iostream>
#ifdef _WIN32
#include "alpaca_console_credentials.hpp"
#include <thread>
#endif

int main() {
    try {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::ConsoleProgress progress;
        progress("Historical panel | resumable five-instrument 2016-2025 acquisition");
        const auto contract = trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const trading::HistoricalPanelDataset dataset{root / "scratch/datasets/extension_panel_2016_2025", contract, trading::PanelOrigin::provider};
#ifdef _WIN32
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        std::cout << "The full archive is a long-running acquisition, separate from routine verification.\n"
            << "Completed responses resume from scratch/datasets/extension_panel_2016_2025.\n"
            << "No strategy fitting, scoring, latest quotes or broker orders.\n";
        const trading::PanelFetchers fetchers{
            [&](const auto& range) { return client.get_market_calendar(range); },
            [&](const auto& request, const auto& token) { return client.get_historical_bars(request, token); },
            [&](const auto& request, const auto& token) { return client.get_corporate_actions(request, token); }};
        const auto summary = dataset.collect(fetchers, [](auto delay) { std::this_thread::sleep_for(delay); }, progress);
#else
        progress("Read-only local archive audit; provider collection uses Windows credentials");
        const auto summary = dataset.audit(progress);
#endif
        const auto directory = trading::HistoricalPanelReport{}.write(summary, root / "scratch/runs");
        std::cout << "Status: " << summary.at("status").get<std::string>()
            << "\nRetained symbol-days: " << summary.at("coverage").size()
            << "\nNetwork requests this invocation: " << summary.at("network_requests")
            << "\nVisual report: " << (directory / "report.html").string()
            << "\nNext: independent archive verification and issuer reconciliation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Panel acquisition stopped: " << error.what() << "\nSaved responses remain available for resumption.\n";
        return 1;
    }
}
