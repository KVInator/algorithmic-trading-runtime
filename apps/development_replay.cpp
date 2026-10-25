#include "momentum_study_report.hpp"
#include "development_study_dataset.hpp"
#include "console_progress.hpp"

#include <exception>
#include <iomanip>
#include <iostream>

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("SPY-IM-01 | offline development replay starting");
        const auto scratch = std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch";
        const auto dataset = trading::DevelopmentStudyDataset{TRADING_PROJECT_ROOT}.load(progress);
        std::cout << "SPY-IM-01 | verified 2022-2023 development study\n"
                  << "Scored sessions: " << dataset.sessions.size()
                  << " | excluded: " << dataset.exclusions.size() << '\n'
                  << "March manifest SHA-256: " << dataset.march_manifest_sha256 << '\n'
                  << "Input manifest SHA-256: " << dataset.manifest_sha256 << '\n';
        const auto result = trading::DevelopmentStudy{}.run(dataset, progress);
        progress("Saving development report");
        const auto report = trading::MomentumStudyReport{dataset, result}.write(scratch / "runs");
        std::cout << "\ndelay_min cost_bps_side active mean_net_daily_bps compounded_pct max_drawdown_pct\n"
                  << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < result.cases.size(); ++i) {
            const auto &replay = result.cases[i];
            const auto &stats = result.statistics[i].overall;
            std::cout << std::setw(9) << replay.delay.count() << ' ' << std::setw(13)
                      << replay.cost_bps_per_side << ' ' << std::setw(6) << stats.active_days << ' '
                      << std::setw(18) << stats.strategy_net.mean * 10000 << ' ' << std::setw(14)
                      << stats.strategy_net.compounded * 100 << ' ' << std::setw(16)
                      << stats.strategy_net.maximum_drawdown * 100 << '\n';
        }
        for (const auto &value : result.primary_bootstrap) {
            std::cout << "\nPrimary " << value.block_length << "-day block bootstrap | 10000 resamples\n"
                      << "95% mean net daily interval (bps): [" << value.mean_net_daily_95.lower * 10000
                      << ", " << value.mean_net_daily_95.upper * 10000 << "]\n";
            if (value.signal_contrast_gross_95) {
                std::cout << "95% gross signal contrast interval (bps): ["
                          << value.signal_contrast_gross_95->lower * 10000 << ", "
                          << value.signal_contrast_gross_95->upper * 10000 << "]\n";
            }
            std::cout << "Undefined contrast resamples: " << value.undefined_contrast_replicates << '\n';
        }
        std::cout << "\nDevelopment decision: " << trading::development_decision_name(result.decision) << '\n'
                  << "Saved report: " << report.string() << '\n'
                  << "Continuous-notional delayed price proxies; all costs are hypothetical.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Development replay failed: " << error.what() << '\n';
        return 1;
    }
}
