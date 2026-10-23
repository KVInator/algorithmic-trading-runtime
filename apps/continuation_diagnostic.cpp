#include "artifact_io.hpp"
#include "continuation_diagnostic_report.hpp"
#include "intraday_continuation.hpp"
#include "market_calendar.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

std::vector<trading::MinuteBar> synthetic_history(const trading::HistoricalBarRequest& session) {
    std::vector<trading::MinuteBar> bars;
    for (int minute = 0; minute < 390; ++minute) {
        bars.emplace_back("SPY", session.start() + std::chrono::minutes{minute},
                          trading::Ohlc{100, 100.5, 100, 100.5}, 100);
    }
    return bars;
}

std::vector<trading::MinuteBar> synthetic_current(const trading::HistoricalBarRequest& session) {
    constexpr std::array<double, 12> closes{101, 101.2, 100.1, 101.4, 101.6, 100.3,
                                           101, 101.2, 101.4, 100.2, 101.5, 101.7};
    std::vector<trading::MinuteBar> bars;
    double previous = 100;
    for (int minute = 0; minute < 390; ++minute) {
        const auto index = std::min(static_cast<std::size_t>(minute / 30), closes.size() - 1);
        const double close = closes[index];
        const double volume = minute >= 90 && minute < 120 ? 400.0 : 50.0;
        bars.emplace_back("SPY", session.start() + std::chrono::minutes{minute},
            trading::Ohlc{previous, std::max(previous, close), std::min(previous, close), close}, volume);
        previous = close;
    }
    return bars;
}

} // namespace

int main() {
    try {
        std::cout << "SPY-TOD-01 | synthetic contract diagnostic\n"
                     "Constructed observations only; no historical performance evaluation.\n" << std::flush;
        trading::IntradayContinuationHistory history;
        const auto calendar = trading::development_calendar({"2022-01-01", "2022-01-31"});
        if (calendar.size() != trading::continuation_history_sessions) {
            throw std::logic_error("synthetic fixture expects twenty January reference sessions");
        }
        for (const auto& day : calendar) {
            const auto request = day.bar_request();
            history.append(request, synthetic_history(request));
        }
        const trading::CalendarSession day{"2022-02-01", "09:30", "16:00"};
        const auto request = day.bar_request();
        const auto bars = synthetic_current(request);
        const trading::IntradayContinuationReplayEngine replay;
        const auto price = replay.run({request, trading::ContinuationVariant::price_only}, history, bars);
        const auto volume = replay.run({request, trading::ContinuationVariant::volume_conditioned}, history, bars);
        const auto report = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch" / "runs", "SPY_TOD_01_synthetic_trace");
        trading::write_new_text_file(report / "trace.html",
            trading::ContinuationDiagnosticReport{price, volume}.html(request));
        trading::write_new_text_file(report / "decisions.csv",
            trading::ContinuationDiagnosticReport{price, volume}.csv());
        std::cout << "Price-only round trips: " << price.trades.size()
                  << "\nVolume-conditioned round trips: " << volume.trades.size()
                  << "\nVisual trace: " << (report / "trace.html").string()
                  << "\nDecision table: " << (report / "decisions.csv").string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Continuation diagnostic failed: " << error.what() << '\n';
        return 1;
    }
}
