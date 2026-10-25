#include "console_progress.hpp"
#include "daily_risk_report.hpp"

#include <algorithm>
#include <exception>
#include <iostream>

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("SPY-RISK-01 | registered daily-risk experiment starting");
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto inputs = trading::DailyRiskDataset{root}.load(progress);
        const auto result = trading::DailyRiskStudy{}.run(inputs, progress);
        progress("Writing forty account paths, forecasts, gates and reproduction evidence");
        const auto directory = trading::DailyRiskReport{result}.write(root / "scratch/runs");
        const bool passes = std::all_of(result.gates.begin(), result.gates.end(), [](const auto& g) { return g.passed; });
        std::cout << "Calendar sessions: 501\nComparison account cases: " << result.accounts.size()
            << "\nEligible primary forecast dates: " << result.primary.forecast_records
            << "\nRegistered screens: " << (passes ? "passed" : "failed; no advancement under this design")
            << "\nVisual report: " << (directory / "report.html").string()
            << "\nManifest: " << (directory / "summary.json").string()
            << "\nOffline experiment completed; frozen inputs were not modified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Daily-risk study stopped: " << error.what() << '\n'; return 1;
    }
}
