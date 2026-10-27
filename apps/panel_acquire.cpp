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
        const auto input_policy = trading::PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
        const trading::HistoricalPanelDataset dataset{root / "scratch/datasets/extension_panel_2016_2025", contract, trading::PanelOrigin::provider};
#ifdef _WIN32
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        if (std::filesystem::exists(root / "scratch/datasets/extension_panel_2016_2025/acquisition_manifest.json")) {
            std::cout << "Existing panel found: authenticating cached responses before exact-time closing recovery.\n";
        } else {
            std::cout << "The full archive is a long-running acquisition, separate from routine verification.\n"
                << "Completed responses resume from scratch/datasets/extension_panel_2016_2025.\n";
        }
        std::cout << "No strategy fitting, scoring, latest quotes or broker orders.\n";
        const trading::PanelFetchers fetchers{
            [&](const auto& range) { return client.get_market_calendar(range); },
            [&](const auto& request, const auto& token) { return client.get_historical_bars(request, token); },
            [&](const auto& request, const auto& token) { return client.get_corporate_actions(request, token); }};
        auto summary = dataset.collect(fetchers, [](auto delay) { std::this_thread::sleep_for(delay); }, progress);
        progress("Collecting the three registered closing-minute recovery windows into a separate vintage");
        summary["closing_mark_recovery"] = dataset.recover_closing_marks(input_policy, summary, fetchers.bars,
            [](auto delay) { std::this_thread::sleep_for(delay); }, true).summary;
        summary["network_requests"] = summary.at("network_requests").get<std::size_t>() +
            summary.at("closing_mark_recovery").at("network_requests").get<std::size_t>();
#else
        progress("Read-only local archive audit; provider collection uses Windows credentials");
        auto summary = dataset.audit(progress);
        if (std::filesystem::exists(root / "scratch/datasets/extension_panel_2016_2025/resolutions/closing_marks/manifest.json")) {
            summary["closing_mark_recovery"] = dataset.recover_closing_marks(input_policy, summary, {}, [](auto) {}, false).summary;
        }
#endif
        const auto directory = trading::HistoricalPanelReport{}.write(summary, root / "scratch/runs");
        if (summary.contains("closing_mark_recovery")) {
            std::cout << "Closing marks recovered: " << summary.at("closing_mark_recovery").at("recovered") << " / "
                << summary.at("closing_mark_recovery").at("required") << '\n';
        }
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
