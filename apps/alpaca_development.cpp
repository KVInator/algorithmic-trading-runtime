#include "alpaca_console_credentials.hpp"
#include "api_request_policy.hpp"
#include "development_dataset.hpp"
#include "development_study_dataset.hpp"

#include <exception>
#include <iostream>
#include <thread>

int main() {
    try {
        std::cout << "SPY-IM-01 development acquisition | 2022-2023\n"
                     "497 planned eligible sessions; frozen March reuse; diagnostic and shortened sessions excluded.\n"
                     "Saved coverage gaps are retained while later sessions are collected.\n";
        const auto datasets = std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch" / "datasets";
        const auto root = datasets / "SPY_1Min_SIP_development_2022-2023";
        if (std::filesystem::exists(root / "study_manifest.json")) {
            const auto study = trading::DevelopmentStudyDataset{TRADING_PROJECT_ROOT}.load();
            std::cout << "Verified frozen revision-2 study: " << study.sessions.size() << " scored sessions\n"
                      << "Study manifest SHA-256: " << study.manifest_sha256 << '\n';
            return 0;
        }
        const trading::DevelopmentDataset dataset{root, datasets / "SPY_1Min_SIP_2022-03"};
        if (std::filesystem::exists(root / "manifest.json")) {
            const auto saved = dataset.load();
            std::cout << "Verified existing dataset: " << saved.sessions.size() << " scored sessions\n"
                      << "March manifest SHA-256: " << saved.march_manifest_sha256 << '\n'
                      << "Development manifest SHA-256: " << saved.manifest_sha256 << '\n';
            return 0;
        }
        const trading::AlpacaMarketDataClient client{load_alpaca_console_credentials()};
        const trading::ApiRequestPolicy policy{[](std::chrono::milliseconds wait) { std::this_thread::sleep_for(wait); }};
        const auto collection = dataset.collect(
            [&](const trading::CalendarRange& range) { return policy.get([&] { return client.get_market_calendar(range); }); },
            [&](const trading::HistoricalBarRequest& request, const std::optional<std::string>& token) {
                return policy.get([&] { return client.get_historical_bars(request, token); });
            }, datasets.parent_path() / "runs",
            [](const std::string& message) { std::cout << message << std::endl; });
        std::cout << "Collection audit complete\n"
                  << "Complete eligible sessions: " << collection.complete_sessions << "/497\n"
                  << "Unresolved sessions: " << collection.unresolved.size() << '\n'
                  << "Saved acquisition audit: " << collection.audit_directory.string() << '\n';
        for (const auto& session : collection.unresolved) {
            std::cout << session.date << ": " << session.audit.observed_bars << '/' << session.audit.expected_minutes
                      << " bars; " << session.audit.missing_minutes.size() << " missing minute slots\n";
        }
        if (!collection.unresolved.empty()) {
            std::cout << "Unresolved gaps block the final dataset manifest and performance evaluation.\n"
                         "No data exclusions were added. Review the complete inventory before repair or protocol revision.\n";
            return 2;
        }
        const auto saved = dataset.load();
        std::cout << "Scored sessions: " << saved.sessions.size() << '\n'
                  << "Exclusions: " << saved.exclusions.size() << '\n'
                  << "March manifest SHA-256: " << saved.march_manifest_sha256 << '\n'
                  << "Development manifest SHA-256: " << saved.manifest_sha256 << '\n'
                  << "Dataset: " << root.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Development acquisition failed: " << error.what() << '\n';
        return 1;
    }
}
