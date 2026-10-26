#include "console_progress.hpp"
#include "artifact_io.hpp"
#include "historical_capability.hpp"
#include "historical_capability_report.hpp"
#include <iostream>
#ifdef _WIN32
#include "alpaca_console_credentials.hpp"
#include <thread>
#endif

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("Historical capability | five instruments, 2016-2025, read-only samples");
#ifdef _WIN32
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "historical_capability");
        std::cout << "Output directory: " << directory.string() << '\n'
                  << "Only review/ is intended for sharing; later-period raw responses remain sealed locally.\n";
        const trading::HistoricalCapabilityFetchers fetchers{
            [&](const auto& range) { return client.get_market_calendar(range); },
            [&](const auto& request, const auto& token) { return client.get_historical_bars(request, token); },
            [&](const auto& request, const auto& token) { return client.get_corporate_actions(request, token); },
            [&](const auto& request) { return client.get_historical_ticks(request); }};
        const trading::HistoricalCapabilityProbe probe{{}, trading::CapabilityOrigin::provider,
            [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }};
        const auto summary = probe.run(directory, fetchers, progress);
        trading::HistoricalCapabilityReport{}.write(directory, summary);
        std::cout << "Status: " << summary.at("status").get<std::string>()
                  << "\nCapability rows: " << summary.at("rows").size()
                  << "\nHTTP attempts: " << summary.at("request_attempts")
                  << "\nVisual report: " << (directory / "review/report.html").string()
                  << "\nShare directory: " << (directory / "review").string()
                  << "\nNo strategy was fitted or scored. Full coverage remains a later audit.\n";
        return summary.at("status") == nlohmann::json("sampled_price_route_ready") ? 0 : 2;
#else
        progress("Live historical access uses the existing Windows credential/HTTP implementation.");
        progress("Run historical_capability_check for constructed offline verification.");
        return 2;
#endif
    } catch (const std::exception& error) {
        std::cerr << "Historical capability stopped: " << error.what() << '\n';
        return 1;
    }
}
