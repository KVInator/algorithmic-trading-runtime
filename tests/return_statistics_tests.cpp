#include "development_study.hpp"
#include "artifact_io.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 2e-14, message);
}
template <typename Operation> void rejects(Operation operation, const char* message) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

std::vector<trading::DailyResearchReturn> records(std::size_t count) {
    std::vector<trading::DailyResearchReturn> result;
    auto date = trading::parse_utc_seconds("2022-03-01T00:00:00Z");
    for (std::size_t i = 0; i < count; ++i) {
        const double gross = static_cast<double>(static_cast<int>(i % 7) - 3) / 1000;
        result.push_back({trading::format_utc_seconds(date).substr(0, 10), i % 3 != 1, gross, gross - 0.0002});
        date += std::chrono::days{1};
    }
    return result;
}

void test_descriptive() {
    auto days = records(4);
    const double returns[]{0.1, -0.2, 0.0, 0.1};
    for (std::size_t i = 0; i < days.size(); ++i) { days[i].active = true; days[i].window_gross_return = days[i].window_net_return = returns[i]; }
    const auto result = trading::describe_returns(days);
    near(result.strategy_net.compounded, -0.032, "compounding must follow the chronological wealth path");
    near(result.strategy_net.maximum_drawdown, 0.2, "drawdown must reference the running wealth peak");
    near(*result.strategy_net.sample_standard_deviation, std::sqrt(0.02), "sample standard deviation needs N-1");
    near(result.strategy_net.mean, 0, "sample mean mismatch");
    near(*result.strategy_net.annualized_daily_sharpe, 0, "nonzero variance with zero mean must give zero Sharpe");
    require(!result.signal_contrast_gross, "all-active sample has no nonpositive signal group");
    auto constant = records(12);
    for (auto& day : constant) { day.active = true; day.window_gross_return = day.window_net_return = 0.001; }
    const auto flat_variance = trading::describe_returns(constant);
    require(flat_variance.strategy_net.sample_standard_deviation == 0 && !flat_variance.strategy_net.annualized_daily_sharpe,
            "constant nonzero returns must have undefined Sharpe and exactly zero variance");
    auto initial_loss = records(1);
    initial_loss[0] = {"2022-03-01", true, -0.2, -0.2};
    const auto single = trading::describe_returns(initial_loss);
    near(single.strategy_net.maximum_drawdown, 0.2, "initial wealth must participate in drawdown");
    require(!single.strategy_net.sample_standard_deviation && !single.strategy_net.annualized_daily_sharpe,
            "one day has undefined sample deviation and Sharpe");
    days = records(8);
    const double concentration[]{0.08, 0.07, 0.06, 0.05, 0.04, 0.03, -0.20, 0};
    for (std::size_t i = 0; i < days.size(); ++i) { days[i].active = true; days[i].window_gross_return = days[i].window_net_return = concentration[i]; }
    const auto winners = trading::describe_returns(days);
    require(winners.largest_five_winners.size() == 5 && winners.largest_five_winners.front().date == "2022-03-01", "largest winners mismatch");
    near(winners.largest_five_net_sum, 0.30, "five-largest arithmetic contribution mismatch");
    near(winners.arithmetic_net_sum, 0.13, "total arithmetic net mismatch");
    near(*winners.largest_five_share_of_arithmetic_net, 0.30 / 0.13, "net contribution shares must not be clipped at one");
    near(*winners.largest_five_share_of_positive_returns, 0.30 / 0.33, "positive-return contribution denominator mismatch");
    days = records(12);
    for (auto& day : days) { day.active = false; }
    const auto cash = trading::describe_returns(days);
    require(cash.active_days == 0 && !cash.mean_gross_active && !cash.mean_net_active &&
        !cash.signal_contrast_gross && !cash.strategy_net.annualized_daily_sharpe &&
        cash.strategy_net.maximum_drawdown == 0 && cash.strategy_net.compounded == 0 &&
        cash.strategy_net.sample_standard_deviation == 0 && cash.largest_five_winners.empty(), "cash or undefined-statistic handling mismatch");
    days = {{"2022-03-01", true, 0.02, 0.0198}, {"2022-03-02", false, -0.01, -0.0102}};
    const auto paired = trading::describe_returns(days);
    near(*paired.signal_contrast_gross, 0.03, "contrast must use gross window returns in both groups");
    near(paired.strategy_net.mean, 0.0099, "cash days must remain in the daily denominator");
    near(*paired.break_even_cost_bps_per_side, 10000 * 0.02 / 2.02, "break-even cost must use the exact two-sided formula");
}

void test_bootstrap(const std::filesystem::path& output) {
    auto five = records(5);
    const auto exact = trading::describe_returns(five);
    const auto collapsed = trading::MovingBlockBootstrap{5}.run(five);
    near(collapsed.mean_net_daily_95.lower, exact.strategy_net.mean, "full-length blocks must preserve the original mean");
    near(collapsed.mean_net_daily_95.upper, exact.strategy_net.mean, "full-length blocks must produce a collapsed interval");
    near(collapsed.signal_contrast_gross_95->lower, *exact.signal_contrast_gross, "paired full-length blocks must preserve contrast");
    require(collapsed.undefined_contrast_replicates == 0 && collapsed.replicates == 10000 && collapsed.seed == 20261020,
            "protocol bootstrap configuration mismatch");
    const auto fixture = records(23); // Neither block length divides N.
    nlohmann::json evidence{{"records", nlohmann::json::array()}, {"bootstrap", nlohmann::json::array()}};
    for (const auto& day : fixture) { evidence["records"].push_back({{"date", day.date}, {"active", day.active},
        {"gross", day.window_gross_return}, {"net", day.window_net_return}}); }
    for (const auto length : {5U, 10U}) {
        const auto one = trading::MovingBlockBootstrap{length}.run(fixture);
        const auto two = trading::MovingBlockBootstrap{length}.run(fixture);
        require(one.mean_net_daily_95.lower == two.mean_net_daily_95.lower &&
                one.mean_net_daily_95.upper == two.mean_net_daily_95.upper, "bootstrap must reset to the fixed seed on each run");
        require(one.undefined_contrast_replicates == 0 && one.signal_contrast_gross_95 &&
                one.mean_net_daily_95.lower < one.mean_net_daily_95.upper, "mixed-group bootstrap interval mismatch");
        // Independently reproduced with a Python MT19937-64 implementation,
        // paired block sampling and interpolated empirical percentiles.
        near(one.mean_net_daily_95.lower, length == 5 ? -0.0006521739130434783 : -0.0006086956521739131,
             "portable seeded bootstrap lower-mean percentile mismatch");
        near(one.mean_net_daily_95.upper, length == 5 ? 0.0004260869565217391 : 0.0003043478260869565,
             "portable seeded bootstrap upper-mean percentile mismatch");
        near(one.signal_contrast_gross_95->lower, length == 5 ? -0.0015396825396825397 : -0.0014365079365079366,
             "paired bootstrap lower-contrast percentile mismatch");
        near(one.signal_contrast_gross_95->upper, length == 5 ? 0.0014524553571428562 : 0.001246031746031746,
             "paired bootstrap upper-contrast percentile mismatch");
        evidence["bootstrap"].push_back({{"block_length", length},
            {"mean_lower", one.mean_net_daily_95.lower}, {"mean_upper", one.mean_net_daily_95.upper},
            {"contrast_lower", one.signal_contrast_gross_95->lower}, {"contrast_upper", one.signal_contrast_gross_95->upper}});
    }
    trading::write_new_text_file(output / "bootstrap_oracle.json", evidence.dump(2) + '\n');
    auto missing = records(23);
    for (auto& day : missing) { day.active = true; }
    const auto absent = trading::MovingBlockBootstrap{5}.run(missing);
    require(absent.undefined_contrast_replicates == 10000 && !absent.signal_contrast_gross_95,
            "missing-group resamples must be counted and suppress the contrast interval");
    missing = records(23);
    for (auto& day : missing) { day.active = false; }
    missing[0].active = true;
    const auto partial = trading::MovingBlockBootstrap{10}.run(missing);
    require(partial.undefined_contrast_replicates > 0 && partial.undefined_contrast_replicates < 10000 &&
            !partial.signal_contrast_gross_95, "even some undefined contrast resamples must suppress the interval");
    rejects([&] { static_cast<void>(trading::MovingBlockBootstrap{10}.run(five)); }, "short samples must fail");
    rejects([] { static_cast<void>(trading::MovingBlockBootstrap{6}); }, "undeclared block length accepted");
}

void test_screen_and_errors() {
    auto days = records(120);
    for (std::size_t i = 0; i < days.size(); ++i) {
        days[i].active = i < 100;
        days[i].window_gross_return = i < 100 ? 0.01 : -0.01;
        days[i].window_net_return = days[i].window_gross_return - 0.0002;
    }
    const auto primary = trading::describe_returns(days);
    using Decision = trading::DevelopmentDecision;
    require(trading::assess_development(primary, primary) == Decision::eligible_for_unchanged_validation,
            "100 active days with all declared positive conditions must pass development");
    auto changed = primary;
    changed.active_days = 99;
    require(trading::assess_development(changed, changed) == Decision::insufficient_active_days, "99 active days must remain insufficient");
    changed = primary; changed.mean_gross_active = 0;
    require(trading::assess_development(changed, primary) == Decision::no_gross_edge, "zero gross edge must stop escalation");
    changed = primary; changed.mean_net_active = 0;
    require(trading::assess_development(changed, primary) == Decision::cost_dominated, "zero primary net must fail");
    require(trading::assess_development(primary, changed) == Decision::delay_stress_failed, "zero delay-stress net must fail");
    changed = primary; changed.signal_contrast_gross = 0;
    require(trading::assess_development(changed, primary) == Decision::nonpositive_signal_contrast, "zero contrast must fail");
    changed.signal_contrast_gross.reset();
    require(trading::assess_development(changed, primary) == Decision::undefined_signal_contrast, "undefined contrast must remain explicit");
    changed = primary; changed.mean_net_active = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { static_cast<void>(trading::assess_development(changed, primary)); }, "nonfinite screening statistics accepted");
    days[1].date = days[0].date;
    rejects([&] { static_cast<void>(trading::describe_returns(days)); }, "duplicate dates accepted");
    days = records(5); days[0].window_net_return = -1;
    rejects([&] { static_cast<void>(trading::describe_returns(days)); }, "nonpositive terminal wealth return accepted");
    days[0].window_net_return = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { static_cast<void>(trading::describe_returns(days)); }, "nonfinite return accepted");
    rejects([] { static_cast<void>(trading::describe_returns({})); }, "empty statistics accepted");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) { throw std::invalid_argument("statistics tests require an explicit artifact root"); }
        const auto output = trading::create_unique_directory(argv[1], "contracts");
        test_descriptive(); test_bootstrap(output); test_screen_and_errors();
        std::cout << "Return statistics contracts passed\nOracle: " << (output / "bootstrap_oracle.json").string() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << "Statistics test failed: " << error.what() << '\n'; return 1; }
}
