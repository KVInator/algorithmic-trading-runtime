#include "continuation_study_report.hpp"
#include "development_study_dataset.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "console_progress.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const trading::ConsoleProgress progress;
        progress("SPY-TOD-01 | offline paired development study starting");
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        if (trading::sha256_file(root / "research/intraday_continuation_protocol.md") !=
            trading::continuation_protocol_sha256()) {
            throw std::invalid_argument("continuation protocol differs from the compiled version; rebuild before scoring");
        }
        const auto calendar_csv = trading::read_text_file(root / "research/scheduled_releases_2022_2023.csv");
        if (trading::sha256(calendar_csv) != trading::continuation_release_calendar_sha256()) {
            throw std::invalid_argument("release calendar differs from the compiled version; rebuild before analysis");
        }
        const trading::ScheduledReleaseCalendar calendar{calendar_csv};
        if (calendar.records().size() != 64) { throw std::invalid_argument("development release calendar must contain 64 records"); }
        const auto dataset = trading::DevelopmentStudyDataset{root}.load(progress);
        progress("Frozen snapshot loaded | 496 eligible; 20 warm-up; 476 scored sessions");
        const auto result = trading::ContinuationStudy{}.run_frozen(dataset, progress);
        progress("Matching exposure, calendar groups, winner and month stresses, covariates and entry timing");
        const auto diagnostics = trading::ContinuationDiagnostics{}.analyze(result, dataset.sessions, calendar);
        progress("Writing visual report, daily returns, matched exposure and diagnostic tables");
        const auto directory = trading::save_continuation_report(dataset, result, diagnostics, calendar_csv, root / "scratch/runs");
        std::cout << std::fixed << std::setprecision(6);
        for (std::size_t variant = 0; variant < 2; ++variant) {
            const auto& stats = result.statistics[variant * 10 + 2].overall;
            std::cout << (variant == 0 ? "Price-only" : "Volume-conditioned") << " | primary\n"
                      << "Active days: " << stats.active_days << " | round trips: " << stats.round_trips << '\n'
                      << "Mean daily net (bp): " << stats.net.mean * 10000 << '\n'
                      << "Compounded net (%): " << stats.net.compounded * 100 << '\n'
                      << "Maximum daily-close drawdown (%): " << stats.net.maximum_drawdown * 100 << '\n'
                      << "Assessment: " << result.variants[variant].status << "\n\n";
        }
        std::cout << "Paired net increment (bp/day): " << result.paired_net_increment * 10000 << '\n'
                  << "Paired gross increment (bp/day): " << result.paired_gross_increment * 10000 << '\n'
                  << "Incremental volume: " << result.volume_increment.status << '\n'
                  << "Visual report: " << (directory / "report.html").string() << '\n'
                  << "Statistics: " << (directory / "summary.json").string() << '\n'
                  << "Development evidence; delayed price proxies and hypothetical costs.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Continuation development failed: " << error.what() << '\n';
        return 1;
    }
}
