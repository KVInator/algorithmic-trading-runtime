#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "market_calendar.hpp"
#include "reversion_preflight_report.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace trading;
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-11; }
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "malformed or noncausal input must fail");
}
std::vector<MinuteBar> full(const HistoricalBarRequest& request) {
    std::vector<MinuteBar> bars;
    for (int i = 0; i < 390; ++i) {
        bars.emplace_back("SPY", request.start() + std::chrono::minutes{i}, Ohlc{100, 100, 100, 100}, 100);
    }
    return bars;
}
void measurement_contracts() {
    const auto request = CalendarSession{"2022-02-01", "09:30", "16:00"}.bar_request();
    auto bars = full(request);
    const OpeningAnchorMeasurement measurement;
    const auto original = measurement.measure(request, std::span{bars}.first(150));
    require(original.anchor_price == 100 && original.values.front() == 0 && original.values.back() == 0,
        "flat prefix produces an exact zero residual");
    bars[200] = MinuteBar{"SPY", bars[200].start(), Ohlc{100000, 100000, 100000, 100000}, 1000000};
    require(measurement.measure(request, std::span{bars}.first(150)).values == original.values,
        "future prices and volume cannot change the completed prefix");
    rejects([&] { static_cast<void>(measurement.measure(request, bars)); });
    rejects([&] { static_cast<void>(measurement.measure(request, std::span{bars}.first(149))); });
    auto prefix = std::vector<MinuteBar>{bars.begin(), bars.begin() + 150};
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const double close = i == 0 ? 80 : i == 1 ? 120 : 100;
        const double volume = i == 0 ? 100 : i == 1 ? 300 : 0;
        prefix[i] = MinuteBar{"SPY", prefix[i].start(), Ohlc{close, close, close, close}, volume};
    }
    const auto weighted = measurement.measure(request, prefix);
    require(weighted.anchor_price == 110 && near(weighted.values.back(), std::log(100.0 / 110)),
        "opening arithmetic close-volume reference has independent weighted identity");
    auto scaled = prefix;
    for (auto& bar : scaled) {
        const double p = bar.prices().close * 1000;
        bar = MinuteBar{"SPY", bar.start(), Ohlc{p, p, p, p}, bar.volume()};
    }
    require(near(measurement.measure(request, scaled).values.back(), weighted.values.back()), "price-scale invariance");
    auto gap = prefix; gap.erase(gap.begin() + 42);
    rejects([&] { static_cast<void>(measurement.measure(request, gap)); });
    auto duplicate = prefix; duplicate[42] = duplicate[41];
    rejects([&] { static_cast<void>(measurement.measure(request, duplicate)); });
    for (auto& bar : prefix) { const auto p = bar.prices(); bar = MinuteBar{"SPY", bar.start(), p, 0}; }
    rejects([&] { static_cast<void>(measurement.measure(request, prefix)); });
    const auto early = CalendarSession{"2022-11-25", "09:30", "13:00"}.bar_request();
    rejects([&] { static_cast<void>(measurement.decision_time(early)); });
    const HistoricalBarRequest wrong{"QQQ", request.start(), request.end_exclusive()};
    rejects([&] { static_cast<void>(measurement.decision_time(wrong)); });
    const auto holiday = CalendarSession{"2022-12-26", "09:30", "16:00"}.bar_request();
    rejects([&] { static_cast<void>(measurement.decision_time(holiday)); });
    const auto summer = CalendarSession{"2022-07-05", "09:30", "16:00"}.bar_request();
    require(measurement.decision_time(summer) == summer.start() + std::chrono::minutes{150},
        "decision clock follows the independently verified daylight-saving session");
}
void estimator_contracts(const ReversionPreflightResult& result) {
    const PooledReversionEstimator estimator;
    auto pairs = result.controls.front().sequences;
    std::reverse(pairs.begin(), pairs.end());
    auto fitted = estimator.fit(pairs);
    require(near(*fitted.slope, 0.8) && near(*fitted.intercept, 0.0002), "session-order invariance of pooled pairs");
    for (auto& sequence : pairs) { for (auto& x : sequence) { x *= 1e8; } }
    fitted = estimator.fit(pairs);
    require(near(*fitted.slope, 0.8) && near(*fitted.half_life_minutes, *result.controls.front().fitted.half_life_minutes),
        "residual-unit changes preserve slope and half-life");
    require(estimator.fit({}).status == ReversionFitStatus::insufficient_pairs, "empty fit is explicitly unavailable");
    const std::vector<std::vector<double>> insufficient{{0, 1}};
    require(estimator.fit(insufficient).status == ReversionFitStatus::insufficient_pairs, "insufficient regression degrees of freedom");
    const std::vector<std::vector<double>> flat{{2, 2, 2, 2, 2}};
    require(estimator.fit(flat).status == ReversionFitStatus::singular_design, "constant regressors are not clipped into a model");
    const std::vector<std::vector<double>> deterministic{{-2, -0.75}, {-1, -0.25}, {0, 0.25}, {1, 0.75}, {2, 1.25}};
    require(estimator.fit(deterministic).status == ReversionFitStatus::zero_innovation_variance, "deterministic fit is not a stochastic OU estimate");
    const std::vector<std::vector<double>> singleton{{1}};
    rejects([&] { static_cast<void>(estimator.fit(singleton)); });
    pairs[0][0] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { static_cast<void>(estimator.fit(pairs)); });
    rejects([&] { static_cast<void>(estimator.expected_log_move(result.controls[1].fitted, -0.004, 1, 30)); });
    rejects([&] { static_cast<void>(estimator.expected_log_move(result.controls[0].fitted, -0.004, -1, 30)); });
    rejects([&] { static_cast<void>(estimator.expected_log_move(result.controls[0].fitted, -0.004, 1, 0)); });
    require(estimator.expected_log_move(result.controls[0].fitted, *result.controls[0].fitted.equilibrium, 1, 30) == 0,
        "equilibrium has zero expected log move");
}
void coverage_and_reporting(const std::filesystem::path& root, ReversionPreflightResult result) {
    auto calendar = development_calendar(CalendarRange{"2022-01-01", "2022-01-31"});
    const auto february = development_calendar(CalendarRange{"2022-02-01", "2022-02-28"});
    calendar.insert(calendar.end(), february.begin(), february.end());
    std::vector<SessionDownload> sessions;
    for (std::size_t i = 0; i < 22; ++i) {
        const auto request = calendar[i].bar_request();
        auto bars = full(request);
        const auto audit = audit_session(request, bars);
        sessions.push_back({request, std::move(bars), {}, audit, request.start(), request.end_exclusive()});
    }
    result.coverage = ReversionPreflight{}.audit_coverage(sessions);
    require(result.coverage.size() == 22 && !result.coverage[19].input_ready && result.coverage[20].input_ready &&
        result.coverage[20].earlier_training_sessions == 20, "twenty strictly earlier complete sessions precede availability");
    auto gap = sessions;
    gap[20].bars.erase(gap[20].bars.begin() + 151);
    require(!ReversionPreflight{}.audit_coverage(gap)[20].reference_clocks_present, "missing reference clock is visible without computing a return");
    std::reverse(sessions.begin(), sessions.end());
    rejects([&] { static_cast<void>(ReversionPreflight{}.audit_coverage(sessions)); });
    sessions.erase(sessions.begin() + 1, sessions.end());
    sessions[0].request = CalendarSession{"2024-01-02", "09:30", "16:00"}.bar_request();
    rejects([&] { static_cast<void>(ReversionPreflight{}.audit_coverage(sessions)); });
    const auto directory = create_unique_directory(root, "report");
    ReversionPreflightReport{result}.write(directory);
    const auto summary = nlohmann::json::parse(read_text_file(directory / "summary.json"));
    require(summary.at("historical_model_fits").get<int>() == 0 && summary.at("historical_strategy_scores").get<int>() == 0 &&
        summary.at("study_status").get<std::string>() == "unregistered_unscored", "preflight report cannot imply registration or performance");
    for (const auto& artifact : summary.at("artifacts")) {
        const auto path = artifact.at("path").get<std::string>();
        require(sha256_file(directory / path) == artifact.at("sha256").get<std::string>(), "artifact hashes match saved evidence");
    }
    const auto controls = nlohmann::json::parse(read_text_file(directory / "controls.json"));
    require(controls.at("controls")[0].at("slope").is_number() && controls.at("controls")[1].at("half_life_minutes").is_null(),
        "optional numerical fields remain scalar or null");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("reversion contracts require an output root"); }
        const auto result = trading::ReversionPreflight{}.controls();
        measurement_contracts();
        estimator_contracts(result);
        coverage_and_reporting(trading::create_unique_directory(argv[1], "reversion-contracts"), result);
        std::cout << "Intraday reversion specification and preflight contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Reversion contract failure: " << error.what() << '\n';
        return 1;
    }
}
