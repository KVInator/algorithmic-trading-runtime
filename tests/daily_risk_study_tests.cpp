#include "daily_risk_report.hpp"
#include "daily_risk_rules.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace trading;
void require(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
void near(double a, double b, double tolerance = 1e-9) { require(std::abs(a-b) <= tolerance, "daily-risk numerical identity failed"); }
template<class F> void rejects(F f) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; }
    require(failed, "invalid study inputs must stop before scoring");
}
RiskSessionReferences session(const CalendarSession& calendar, double previous, std::size_t index, double dividend, bool full = true,
    bool gap = false, double shock = 0) {
    std::vector<MinuteBar> bars;
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(calendar.close_utc() - calendar.open_utc()).count();
    const auto i_day = static_cast<double>(index);
    auto price = [&](int minute) {
        const double fraction = static_cast<double>(minute) / static_cast<double>(minutes);
        return previous * std::exp(.012 * std::sin(i_day*.18) + .009 * std::cos(i_day*.67) * fraction +
            .003 * std::sin(static_cast<double>(minute)*.75) + shock * fraction) - dividend;
    };
    for (int i = 0; i < minutes; ++i) {
        if ((!full && i + 1 != minutes) || (gap && i >= 22 && i <= 25)) { continue; }
        const double open = price(i), close = price(i+1);
        bars.emplace_back("SPY", calendar.open_utc() + std::chrono::minutes{i}, Ohlc{open, std::max(open,close), std::min(open,close), close}, 1000);
    }
    return {calendar, std::move(bars)};
}
RiskStudyInputs fixture(bool all_missing = false) {
    RiskStudyInputs inputs{}; inputs.synthetic = true; inputs.boundary_close = 100;
    inputs.protocol_sha256 = "synthetic"; inputs.development_manifest_sha256 = "synthetic";
    inputs.acquisition_manifest_sha256 = "synthetic"; inputs.issuer_reference_sha256 = "synthetic";
    inputs.distributions = {{"constructed-weekend-payment", "2022-01-04", "2022-01-04", "2022-01-08", "2022-01-08", .4},
        {"constructed-terminal-claim", "2023-12-15", "2023-12-18", "2024-01-31", "2024-01-31", .6}};
    double previous = inputs.boundary_close; std::size_t index = 0;
    for (const auto& c : risk_rules::calibration_calendar()) {
        inputs.calibration.push_back(session(c, previous, index, 0, index >= 232, all_missing));
        previous = inputs.calibration.back().close(); ++index;
    }
    for (const auto& c : risk_rules::development_calendar()) {
        double dividend = 0;
        for (const auto& d : inputs.distributions) { if (d.ex_date == c.date()) { dividend += d.usd_per_share; } }
        inputs.development.push_back(session(c, previous, index++, dividend, true, all_missing || c.date() == "2023-06-05"));
        previous = inputs.development.back().close();
    }
    return inputs;
}
void check_registration() {
    const std::filesystem::path root{TRADING_PROJECT_ROOT};
    require(sha256_file(root / "research/daily_risk_protocol.md") + ":" + sha256_file(root / "research/daily_risk_protocol.json") == risk_rules::protocol_identity,
        "implementation must retain the published registration bytes");
    const auto rules = nlohmann::json::parse(read_text_file(root / "research/daily_risk_protocol.json"));
    require(rules.at("history_sessions") == risk_rules::history && rules.at("calibration_sessions") == 252 &&
        rules.at("development_sessions") == 501 && rules.at("initial_cash_usd") == risk_rules::initial_cash &&
        rules.at("cost_bps_per_side") == risk_rules::costs && rules.at("execution_delay_minutes") == risk_rules::delays &&
        rules.at("frozen_development_manifest_sha256") == risk_rules::development_digest &&
        rules.at("risk_acquisition_manifest_sha256") == risk_rules::acquisition_digest && rules.at("issuer_reference_sha256") == risk_rules::issuer_digest,
        "compiled study settings must match registered settings");
}
void check_features(const DailyRiskResult& result) {
    require(result.accounts.size() == 40 && result.forecasts.size() == 1002 && result.measurements.size() == 753, "complete study shapes");
    const auto& first = result.forecasts.front();
    require(first.history_first == "2021-12-03" && first.history_last == "2021-12-31" && first.date == "2022-01-03" &&
        first.cutoff_utc == "2021-12-31T21:05:00Z", "first decision is formed from exactly twenty earlier exchange dates");
    double hd = 0, hi = 0;
    for (std::size_t i = 232; i < 252; ++i) { hd += result.measurements[i].daily2; hi += *result.measurements[i].rv5; }
    near(first.h_daily, hd/20); near(*first.h_intraday, hi/20);
    std::size_t fallback = 0, eligible = 0;
    bool gap_label_missing = false, short_cutoff = false;
    for (std::size_t i = 0; i < 501; ++i) {
        const auto& f = result.forecasts[i];
        if (!f.h_intraday) {
            ++fallback; require(f.missing_history_dates == std::vector<std::string>{"2023-06-05"}, "fallback records the incomplete prior date");
            require(f.date > "2023-06-05", "current-session missing risk cannot alter its existing target");
            near(f.used_h, f.h_daily); near(f.intraday_weight, f.daily_weight);
            require(!f.qlike_increment && !f.squared_error_increment && !f.endpoint_qlike_increment, "all native losses use the same eligibility mask");
        }
        if (f.qlike_increment) { ++eligible; require(f.h_intraday && f.future_rv5 && f.squared_error_increment && f.endpoint_qlike_increment, "common native forecast mask"); }
        if (f.date == "2023-06-05") { gap_label_missing = !f.future_rv5 && f.h_intraday && !f.qlike_increment; }
        if (f.date == "2023-07-05") { short_cutoff = f.cutoff_utc == "2023-07-03T17:05:00Z"; }
        require(result.forecasts[i+501].date == f.date && result.forecasts[i+501].h_daily == f.h_daily && result.forecasts[i+501].h_intraday == f.h_intraday,
            "execution-delay stress does not change a prior-close target");
    }
    require(fallback == 20 && eligible == 479 && gap_label_missing && short_cutoff, "known endpoint gap affects exactly twenty future targets and one label");
    require(result.forecasts[0].future_endpoint2 != result.forecasts[501].future_endpoint2, "each delay has its own execution-aligned outcome");
    require(!result.forecasts[500].future_rv5 && !result.forecasts[500].future_endpoint2 && !result.forecasts[500].qlike_increment, "truncated terminal label stays unavailable");
    require(result.primary.forecast_records == eligible && result.forecast_statistics[0].eligible_records == eligible, "primary uses common native mask");
}
void check_accounts(const DailyRiskResult& result) {
    for (const auto& a : result.accounts) {
        require(a.days.size() == 501 && a.days[250].date.starts_with("2022") && a.days[251].date.starts_with("2023"), "all account dates and year boundaries retained");
        double daily_cost = 0, ledger_cost = 0;
        for (const auto& d : a.days) {
            near(d.overnight_pnl_usd + d.intraday_pnl_usd, d.close.equity_usd - d.previous_equity_usd);
            near(d.net_return, d.close.equity_usd / d.previous_equity_usd - 1);
            near(d.close.liquid_wealth_usd + d.close.receivable_usd, d.close.equity_usd, 1e-7);
            require(d.close.cash_usd >= 0 && d.close.shares >= 0 && d.target_weight >= 0 && d.target_weight <= 1, "unlevered long/cash account bounds");
            daily_cost += d.cost_usd;
        }
        std::size_t buys = 0, sells = 0; bool weekend = false;
        double terminal_entitlement = 0;
        for (const auto& e : a.ledger) {
            near(e.cost_usd, e.traded_notional_usd * a.cost_bps / 10000, 1e-8); ledger_cost += e.cost_usd;
            if (e.kind == "buy") { ++buys; } if (e.kind == "sell") { ++sells; }
            if (e.kind == "dividend_payment" && format_utc_seconds(e.at) == "2022-01-08T00:00:00Z") { weekend = true; }
            if (e.kind == "dividend_ex" && e.action_id == "constructed-terminal-claim") { terminal_entitlement = e.value.shares * .6; }
            if (e.kind == "buy" || e.kind == "sell" || e.kind == "hold") {
                const auto row = std::find_if(a.days.begin(), a.days.end(), [&](const auto& d) { return d.date == format_utc_seconds(e.at).substr(0,10); });
                require(row != a.days.end(), "orders only occur on exchange sessions");
                const bool terminal = e.at == parse_utc_seconds("2023-12-29T20:56:00Z");
                const auto expected = terminal ? 0 : row->target_weight;
                near(e.value.shares * e.reference_price_usd / e.value.liquid_wealth_usd, expected, 1e-12);
            }
        }
        near(daily_cost, ledger_cost, 1e-7);
        require(weekend && a.days.back().terminal_liquidation && a.days.back().close.shares == 0 && a.days.back().close.receivable_usd > 0, "weekend cash payment and terminal unpaid claim preserved");
        near(a.days.back().close.receivable_usd, terminal_entitlement);
        if (a.account == "passive_equity") { require(buys == 1 && sells == 1, "passive comparator buys once and liquidates once without reinvesting distributions"); }
    }
    require(result.statistics.size() == 120 && result.omissions.size() == 30 && result.gates.size() == 15, "annual statistics and registered diagnostics complete");
}
void check_bootstrap(const DailyRiskResult& result) {
    const auto& a = risk_account_case(result, "intraday_risk", 1, 1);
    const auto& b = risk_account_case(result, "daily_only", 1, 1);
    const auto& c = risk_account_case(result, "static_risk_budget", 1, 1);
    for (const auto& boot : result.uncertainty) {
        MovingBlockIndexSampler indices{501, boot.block_length, boot.seed};
        std::vector<double> daily, fixed, loss;
        for (std::size_t i = 0; i < boot.replicates; ++i) {
            std::vector<double> x, y, z; double total = 0; std::size_t count = 0;
            for (const auto k : indices.next()) {
                x.push_back(a.days[k].net_return); y.push_back(b.days[k].net_return); z.push_back(c.days[k].net_return);
                if (result.forecasts[k].qlike_increment) { total += *result.forecasts[k].qlike_increment; ++count; }
            }
            daily.push_back(certainty_equivalent(x)-certainty_equivalent(y)); fixed.push_back(certainty_equivalent(x)-certainty_equivalent(z));
            require(count > 0, "fixture bootstrap has eligible labels"); loss.push_back(total/static_cast<double>(count));
        }
        const auto d = percentile_interval(daily), s = percentile_interval(fixed), q = percentile_interval(loss);
        near(boot.cer_over_daily.lower,d.lower); near(boot.cer_over_daily.upper,d.upper); near(boot.cer_over_static.lower,s.lower);
        require(boot.qlike_increment && boot.undefined_loss_replicates == 0, "loss uncertainty is defined on paired full-calendar blocks");
        near(boot.qlike_increment->lower,q.lower); near(boot.qlike_increment->upper,q.upper);
    }
}
void check_causality(RiskStudyInputs& inputs, const DailyRiskResult& result) {
    constexpr std::size_t changed = 100;
    const auto calendar = inputs.development[changed].calendar();
    inputs.development[changed] = session(calendar, inputs.development[changed-1].close(), 252+changed, 0, true, false, .08);
    const auto perturbed = DailyRiskStudy{16}.run(inputs);
    for (std::size_t i = 0; i <= changed; ++i) {
        near(perturbed.forecasts[i].h_daily, result.forecasts[i].h_daily, 0);
        near(perturbed.forecasts[i].intraday_weight, result.forecasts[i].intraday_weight, 0);
    }
    require(perturbed.forecasts[changed+1].h_daily != result.forecasts[changed+1].h_daily, "the next decision incorporates the completed changed session");
    near(perturbed.static_weight, result.static_weight, 0);
}
void check_unavailable_loss() {
    const auto result = DailyRiskStudy{16}.run(fixture(true));
    require(!result.primary.qlike_increment && result.primary.forecast_records == 0, "all missing labels stay unavailable");
    near(result.primary.cer_over_daily, 0, 0);
    for (const auto& b : result.uncertainty) {
        require(!b.qlike_increment && b.undefined_loss_replicates == b.replicates, "undefined bootstrap losses cannot become zero losses");
        near(b.cer_over_daily.lower, 0, 0); near(b.cer_over_daily.upper, 0, 0);
    }
    require(std::none_of(result.gates.begin(), result.gates.end(), [](const auto& g) { return g.name.find("QLIKE") != std::string::npos && g.passed; }),
        "unavailable forecasts cannot pass advancement");
}
void check_report(const DailyRiskResult& result, const std::filesystem::path& output) {
    const auto directory = save_daily_risk_report(result, output);
    const auto j = nlohmann::json::parse(read_text_file(directory / "summary.json"));
    require(j.at("historical_strategy_scored") == false && j.at("status") == "synthetic_contract_fixture", "synthetic fixtures cannot appear as historical results");
    require(j.at("fallbacks").size() == 20 && j.at("statistics").size() == 120, "manifest includes fallback evidence and all statistics");
    for (const auto& a : j.at("artifacts")) { require(sha256(read_text_file(directory / a.at("file").get<std::string>(), 64U * 1024U * 1024U)) == a.at("sha256").get<std::string>(), "published artifact digests match bytes"); }
    const auto html = read_text_file(directory / "report.html");
    require(html.find("<svg") != std::string::npos && html.find("<script") == std::string::npos && html.find("No historical result") != std::string::npos,
        "offline report has charts and explicit fixture labeling");
    std::cout << "Synthetic visual fixture: " << (directory / "report.html").string() << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3 || (argc == 3 && std::string_view{argv[2]} != "--registered-bootstrap")) {
            throw std::invalid_argument("study contracts require an output directory and optional --registered-bootstrap");
        }
        check_registration(); auto inputs = fixture();
        const auto result = DailyRiskStudy{argc == 3 ? 10000U : 64U}.run(inputs);
        check_features(result); check_accounts(result); check_bootstrap(result); check_report(result, argv[1]); check_causality(inputs, result); check_unavailable_loss();
        inputs.synthetic = false;
        rejects([&] { static_cast<void>(DailyRiskStudy{}.run(inputs)); });
        inputs.protocol_sha256 = risk_rules::protocol_identity;
        inputs.development_manifest_sha256 = risk_rules::development_digest;
        inputs.acquisition_manifest_sha256 = risk_rules::acquisition_digest;
        inputs.issuer_reference_sha256 = risk_rules::issuer_digest;
        rejects([&] { static_cast<void>(DailyRiskStudy{64}.run(inputs)); });
        std::cout << "Daily-risk study contracts passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "Daily-risk study contract failure: " << e.what() << '\n'; return 1; }
}
