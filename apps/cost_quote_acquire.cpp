#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "cost_quote_report.hpp"
#include "cost_quote_sample.hpp"
#include <iostream>
#ifdef _WIN32
#include "alpaca_console_credentials.hpp"
#include <thread>
#endif
int main(int argc, char **argv) {
    try {
        if (argc > 2) {
            throw std::invalid_argument("usage: cost_quote_acquire [new-or-existing-quote-directory]");
        }
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::ConsoleProgress progress;
        progress("M6 | fixed pre-2024 SIP quote windows; no strategy scoring");
        const auto plan =
            trading::CostQuotePlan::from_panel(root / "scratch/datasets/extension_panel_2016_2025");
#ifdef _WIN32
        const auto dataset = argc == 2 ? std::filesystem::absolute(argv[1])
                                       : root / "scratch/datasets/cost_quote_sample_2017_2023";
        const auto output = trading::create_unique_directory(root / "scratch/runs", "cost_quotes");
        std::cout << "Quote archive: " << dataset.string() << "\nReport directory: " << output.string()
                  << "\nPlanned windows: " << plan.windows().size() << std::endl;
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        const auto summary = trading::CostQuoteSample{dataset, plan}.run(
            true,
            [&](const auto &request, const auto &token) {
                return client.get_historical_ticks(request, token);
            },
            [](auto duration) { std::this_thread::sleep_for(duration); }, progress);
        trading::CostQuoteReport{}.write(output, summary);
        std::cout << "Status: " << summary.at("status").get<std::string>()
                  << "\nRetained HTTP attempts: " << summary.at("retained_http_attempts")
                  << "\nNew HTTP requests: " << summary.at("network_requests_this_invocation")
                  << "\nVisual report: " << (output / "report.html").string()
                  << "\nManifest: " << (output / "summary.json").string() << '\n';
        return 0;
#else
        static_cast<void>(argv);
        static_cast<void>(plan);
        progress("Historical access uses the retained Windows credential/HTTP route; run "
                 "cost_execution_check for offline verification.");
        return 2;
#endif
    } catch (const std::exception &error) {
        std::cerr << "Quote sample stopped: " << error.what()
                  << "; retained response envelopes are preserved for resume.\n";
        return 1;
    }
}
