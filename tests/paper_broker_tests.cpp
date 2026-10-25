#include "paper_broker_diagnostic.hpp"
#include "paper_transport_report.hpp"
#include "simulated_paper_wire.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument("broker contracts require output root");
        trading::SimulatedPaperWire peer{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", 200000};
        peer.on_quote({1, 1, 10000, 10001, 10, 10});
        const auto before = peer.snapshot();
        bool rejected = false;
        try {
            static_cast<void>(peer.request(trading::AlpacaPaperRequests{}.submit(
                {"invalid-short", "SPY", trading::execution::Side::sell, 1, 10000, 0})));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        if (!rejected || peer.snapshot() != before)
            throw std::logic_error("failed matching must not publish an order, identity or broker balance");
        const auto root = trading::create_unique_directory(argv[1], "paper-broker");
        const auto first = trading::PaperBrokerDiagnostic{root / "first"}.run();
        const auto second = trading::PaperBrokerDiagnostic{root / "second"}.run();
        if (first.cases.size() != 6 || first.checks.empty() || first.cases != second.cases ||
            first.checks != second.checks)
            throw std::logic_error(
                "repeated synthetic schedules must reproduce all saved states and identities");
        trading::PaperTransportReport{first}.write(root / "first");
        const auto summary = nlohmann::json::parse(trading::read_text_file(root / "first/summary.json"));
        if (summary.at("network_requests_sent").get<int>() != 0)
            throw std::logic_error("offline broker report must not claim network access");
        std::cout << "Simulated paper protocol lifecycle, cancellation race, duplicate, recovery, "
                     "uncertainty and stop contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Simulated broker contracts failed: " << e.what() << '\n';
        return 1;
    }
}
