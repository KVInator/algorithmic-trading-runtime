#include "alpaca_console_credentials.hpp"
#include "paper_market_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_market_probe");
        std::cout << "Alpaca paper | read-only current calendar and SIP quote probe\nNo orders or session "
                     "enrollment.\n";
        trading::PaperMarketResult result{};
        try {
            const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
            trading::PaperCapture capture{directory / "capture"};
            result = trading::PaperMarketProbe{
                [&](const auto& request) { return client.get_paper_market(request); },
                [] { return trading::paper_receipt_now(); }, capture}
                         .run();
        } catch (const std::exception& e) {
            result.detail = e.what();
        }
        result.native_probe = true;
        trading::PaperMarketReport{result}.write(directory);
        std::cout << "Probe status: "
                  << (result.access_verified ? "read-only access verified"
                                             : "stopped; review retained evidence")
                  << "\nDetail: " << result.detail
                  << "\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        for (const auto& observation : result.cases) {
            if (observation.contains("quote_usable")) {
                std::cout << "Quote usable at saved evaluation: "
                          << (observation.at("quote_usable").get<bool>() ? "yes" : "no")
                          << "\nQuote denials: " << observation.at("denials").dump() << '\n';
            }
        }
        return result.access_verified ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Paper market probe failed: " << e.what() << '\n';
        return 1;
    }
}
