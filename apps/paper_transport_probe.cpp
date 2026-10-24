#include "alpaca_console_credentials.hpp"
#include "alpaca_paper_wire.hpp"
#include "artifact_io.hpp"
#include "paper_transport_report.hpp"
#include <iostream>
#include <thread>

int main() {
    try {
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "paper_transport_probe");
        trading::PaperTransportResult result{};
        result.native_probe = true;
        trading::PaperCapture capture{directory / "capture"};
        std::cout << "Alpaca paper transport | read-only connection probe\nNo "
                     "orders are submitted or canceled.\n";
        try {
            trading::AlpacaPaperWire wire{load_alpaca_console_credentials()};
            const auto clock = [] { return trading::paper_receipt_now(); };
            const auto wait = [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); };
            trading::PaperHttpClient http{wire, capture, clock, wait};
            std::cout << "Reading independent paper account\n";
            const auto account_response = http.exchange(trading::AlpacaPaperRequests{}.account());
            if (account_response.status != 200)
                throw std::runtime_error("paper account query did not return 200");
            const auto account = trading::AlpacaPaperDecoder{}.account(account_response.body);
            trading::execution::PaperExecutionConfiguration config{account.id, account.cash.text(), {20, 4}};
            auto runtime = trading::execution::JournaledPaperRuntime::create(
                trading::execution::create_journal_file(directory / "runtime.journal"), config);
            trading::PaperSession session{runtime, wire, capture, clock, wait};
            std::cout << "Authorizing stream and subscribing to trade_updates\n";
            session.connect();
            result.checks.push_back("stream authorization and subscription acknowledged");
            std::cout << "Collecting account, positions and open orders twice\n";
            const bool matched = session.synchronize();
            if (matched)
                result.checks.push_back("stable flat baseline matches exact ledger");
            result.successful = matched;
            result.cases.push_back({{"case", "read_only_connection"},
                                    {"exact_baseline_match", matched},
                                    {"capture_records", capture.records()},
                                    {"state", runtime.snapshot()}});
            session.disconnect();
            result.detail = matched ? "Read-only protocol path completed; no "
                                      "observed order lifecycle."
                                    : "Paper baseline does not match a flat ledger; "
                                      "no orders attempted. "
                                      "Existing positions/orders were not altered.";
        } catch (const std::exception& e) {
            result.successful = false;
            result.detail = e.what();
        }
        trading::PaperTransportReport{result}.write(directory);
        std::cout << "Probe status: "
                  << (result.successful ? "read-only checks completed" : "stopped; review retained evidence")
                  << "\nVisual report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return result.successful ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Paper probe failed: " << e.what() << '\n';
        return 1;
    }
}
