#include "daily_risk_study.hpp"
#include "daily_risk_rules.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace trading {
namespace {
using namespace std::chrono;
sys_days action_date(const std::string& date) {
    // Retained action metadata can predate the bounded price-session calendar.
    return floor<days>(parse_utc_seconds(date + "T00:00:00Z"));
}
double dividend_on(const RiskStudyInputs& inputs, const std::string& date) {
    double total = 0;
    for (const auto& d : inputs.distributions) { if (d.ex_date == date) { total += d.usd_per_share; } }
    return total;
}
void validate(const RiskStudyInputs& inputs, std::size_t replicates) {
    if (!inputs.synthetic && (replicates != 10000 || inputs.protocol_sha256 != risk_rules::protocol_identity ||
        inputs.development_manifest_sha256 != risk_rules::development_digest ||
        inputs.acquisition_manifest_sha256 != risk_rules::acquisition_digest || inputs.issuer_reference_sha256 != risk_rules::issuer_digest)) {
        throw std::invalid_argument("historical scoring requires the registered inputs, protocol and 10,000 resamples");
    }
    const auto calibration = risk_rules::calibration_calendar();
    const auto development = risk_rules::development_calendar();
    auto matches = [](const auto& actual, const auto& expected) {
        if (actual.size() != expected.size()) { return false; }
        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (actual[i].calendar().date() != expected[i].date() || actual[i].calendar().open_utc() != expected[i].open_utc() ||
                actual[i].calendar().close_utc() != expected[i].close_utc()) { return false; }
        }
        return true;
    };
    if (!matches(inputs.calibration, calibration) || !matches(inputs.development, development) ||
        !std::isfinite(inputs.boundary_close) || inputs.boundary_close <= 0) {
        throw std::invalid_argument("daily risk requires the complete registered calibration and development calendars");
    }
    for (std::size_t i = 0; i < inputs.development.size(); ++i) {
        const auto& session = inputs.development[i];
        const auto open = session.calendar().open_utc();
        if (!session.open_at(open) || !session.open_at(open + minutes{1}) || !session.open_at(open + minutes{2}) ||
            (i + 1 == inputs.development.size() && !session.open_at(session.calendar().close_utc() - minutes{4}))) {
            throw std::invalid_argument("missing required account reference on " + session.calendar().date());
        }
    }
    std::set<std::string> ids;
    for (const auto& d : inputs.distributions) {
        const auto ex = action_date(d.ex_date), pay = action_date(d.payable_date);
        if (d.id.empty() || !ids.insert(d.id).second || !std::isfinite(d.usd_per_share) || d.usd_per_share <= 0 || pay < ex) {
            throw std::invalid_argument("invalid or duplicate cash distribution");
        }
        if (d.ex_date >= "2021-01-01" && d.ex_date <= "2023-12-31" &&
            std::none_of(calibration.begin(), calibration.end(), [&](const auto& s) { return s.date() == d.ex_date; }) &&
            std::none_of(development.begin(), development.end(), [&](const auto& s) { return s.date() == d.ex_date; })) {
            throw std::invalid_argument("ex-date is not an exchange session");
        }
    }
}
std::vector<double> returns(const RiskAccountCase& account, const std::string& year = "") {
    std::vector<double> values;
    for (const auto& day : account.days) { if (day.date.starts_with(year)) { values.push_back(day.net_return); } }
    return values;
}
RiskContrast contrast(const DailyRiskResult& result, double cost, const std::string& year = "") {
    const auto a = returns(risk_account_case(result, "intraday_risk", 1, cost), year);
    const auto b = returns(risk_account_case(result, "daily_only", 1, cost), year);
    const auto c = returns(risk_account_case(result, "static_risk_budget", 1, cost), year);
    long double loss = 0; std::size_t count = 0;
    for (const auto& f : result.forecasts) {
        if (f.delay_minutes == 1 && f.date.starts_with(year) && f.qlike_increment) { loss += *f.qlike_increment; ++count; }
    }
    return {certainty_equivalent(a) - certainty_equivalent(b), certainty_equivalent(a) - certainty_equivalent(c),
        count ? std::optional<double>{static_cast<double>(loss / count)} : std::nullopt, count};
}
RiskAccountCase run_account(const RiskStudyInputs& inputs, const DailyRiskResult& result, std::string name, int delay, double cost) {
    LongCashAccount account{risk_rules::initial_cash};
    RiskAccountCase path{std::move(name), delay, cost, {}, {}};
    double previous_equity = risk_rules::initial_cash;
    double previous_close = inputs.calibration.back().close();
    auto next_date = parse_session_date(inputs.development.front().calendar().date());
    const auto forecast_offset = delay == 1 ? std::size_t{0} : inputs.development.size();
    for (std::size_t i = 0; i < inputs.development.size(); ++i) {
        const auto& session = inputs.development[i];
        const auto& calendar = session.calendar();
        const auto date = parse_session_date(calendar.date());
        const auto ledger_start = account.ledger().size();
        double continuity_price = previous_close;
        for (; next_date <= date; next_date += days{1}) {
            double ex_total = 0;
            bool payment = false;
            for (const auto& d : inputs.distributions) {
                if (d.ex_date >= inputs.development.front().calendar().date() && action_date(d.ex_date) == next_date) { ex_total += d.usd_per_share; }
                if (d.ex_date >= inputs.development.front().calendar().date() && action_date(d.payable_date) == next_date) { payment = true; }
            }
            continuity_price -= ex_total;
            if (continuity_price <= 0) { throw std::invalid_argument("nonpositive theoretical ex-date continuity reference"); }
            for (const auto& d : inputs.distributions) {
                if (d.ex_date >= inputs.development.front().calendar().date() && action_date(d.ex_date) == next_date) {
                    account.accrue_dividend(next_date, d.id, action_date(d.payable_date), d.usd_per_share, continuity_price);
                }
            }
            if (payment) { account.settle_dividends(next_date, continuity_price); }
        }
        const auto opening_price = *session.open_at(calendar.open_utc());
        account.mark(calendar.open_utc(), opening_price);
        const auto opening_equity = account.value(opening_price).equity_usd;
        const auto& forecast = result.forecasts[forecast_offset + i];
        const auto weight = path.account == "intraday_risk" ? forecast.intraday_weight : path.account == "daily_only" ? forecast.daily_weight :
            path.account == "static_risk_budget" ? result.static_weight : 1.0;
        if (path.account != "passive_equity" || i == 0) {
            const auto execution = calendar.open_utc() + minutes{delay};
            account.rebalance(execution, *session.open_at(execution), weight, cost);
        }
        const bool terminal = i + 1 == inputs.development.size();
        if (terminal) {
            const auto liquidation = calendar.close_utc() - minutes{4};
            account.rebalance(liquidation, *session.open_at(liquidation), 0, cost);
        }
        account.mark(calendar.close_utc(), session.close());
        const auto close = account.value(session.close());
        double paid_cost = 0, notional = 0;
        for (auto j = ledger_start; j < account.ledger().size(); ++j) {
            paid_cost += account.ledger()[j].cost_usd; notional += account.ledger()[j].traded_notional_usd;
        }
        path.days.push_back({calendar.date(), previous_equity, opening_equity, close, close.equity_usd / previous_equity - 1,
            opening_equity - previous_equity, close.equity_usd - opening_equity, paid_cost, notional, weight,
            close.shares * session.close() / close.liquid_wealth_usd, path.account == "intraday_risk" && !forecast.h_intraday, terminal});
        previous_equity = close.equity_usd; previous_close = session.close();
    }
    path.ledger = account.ledger();
    return path;
}
RiskCaseStatistics statistics(const RiskAccountCase& account, const std::string& period) {
    auto values = returns(account, period == "all" ? "" : period);
    RiskCaseStatistics s{account.account, account.delay_minutes, account.cost_bps, period, values.size(),
        describe_return_series(values), certainty_equivalent(values), 0, 0, 0, 0, 0, 0, 0};
    for (const auto& d : account.days) {
        if (period != "all" && !d.date.starts_with(period)) { continue; }
        s.traded_notional_usd += d.traded_notional_usd;
        s.turnover_equity_units += d.traded_notional_usd / d.previous_equity_usd;
        s.cost_usd += d.cost_usd; s.mean_closing_liquid_stock_weight += d.closing_stock_weight;
    }
    s.mean_closing_liquid_stock_weight /= static_cast<double>(values.size());
    std::sort(values.begin(), values.end());
    s.worst_daily_return = values.front();
    const double index = .05 * static_cast<double>(values.size() - 1);
    const auto low = static_cast<std::size_t>(std::floor(index)), high = static_cast<std::size_t>(std::ceil(index));
    s.daily_return_5th_percentile = values[low] + (index - static_cast<double>(low)) * (values[high] - values[low]);
    const auto count = std::min(std::size_t{5}, values.size());
    s.mean_bottom_five_returns = std::accumulate(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count), 0.0) / static_cast<double>(count);
    return s;
}
RiskOmission omission(const DailyRiskResult& result, std::string kind, std::string label, std::vector<std::string> dates) {
    const auto& a = risk_account_case(result, "intraday_risk", 1, 1);
    const auto& b = risk_account_case(result, "daily_only", 1, 1);
    const auto& c = risk_account_case(result, "static_risk_budget", 1, 1);
    std::vector<double> x, y, z; long double loss = 0; std::size_t count = 0;
    for (std::size_t i = 0; i < a.days.size(); ++i) {
        if (std::find(dates.begin(), dates.end(), a.days[i].date) != dates.end()) { continue; }
        x.push_back(a.days[i].net_return); y.push_back(b.days[i].net_return); z.push_back(c.days[i].net_return);
        if (result.forecasts[i].qlike_increment) { loss += *result.forecasts[i].qlike_increment; ++count; }
    }
    return {std::move(kind), std::move(label), std::move(dates),
        {certainty_equivalent(x) - certainty_equivalent(y), certainty_equivalent(x) - certainty_equivalent(z),
         count ? std::optional<double>{static_cast<double>(loss / count)} : std::nullopt, count}};
}
RiskBootstrap bootstrap(const DailyRiskResult& result, std::size_t block, std::size_t replicates, std::uint64_t seed) {
    const auto a = returns(risk_account_case(result, "intraday_risk", 1, 1));
    const auto b = returns(risk_account_case(result, "daily_only", 1, 1));
    const auto c = returns(risk_account_case(result, "static_risk_budget", 1, 1));
    MovingBlockIndexSampler sampler{a.size(), block, seed};
    std::vector<double> cer_daily, cer_static, losses, x(a.size()), y(a.size()), z(a.size());
    std::size_t undefined = 0;
    for (std::size_t r = 0; r < replicates; ++r) {
        const auto indices = sampler.next();
        long double loss = 0; std::size_t count = 0;
        for (std::size_t j = 0; j < indices.size(); ++j) {
            const auto k = indices[j]; x[j] = a[k]; y[j] = b[k]; z[j] = c[k];
            if (result.forecasts[k].qlike_increment) { loss += *result.forecasts[k].qlike_increment; ++count; }
        }
        const auto cer = certainty_equivalent(x);
        cer_daily.push_back(cer - certainty_equivalent(y)); cer_static.push_back(cer - certainty_equivalent(z));
        if (count) { losses.push_back(static_cast<double>(loss / count)); } else { ++undefined; }
    }
    return {block, replicates, seed, percentile_interval(std::move(cer_daily)), percentile_interval(std::move(cer_static)),
        undefined == 0 ? std::optional<PercentileInterval>{percentile_interval(std::move(losses))} : std::nullopt, undefined};
}
} // namespace

double certainty_equivalent(std::span<const double> values) {
    if (values.size() < 2) { throw std::invalid_argument("CER requires at least two daily returns"); }
    long double sum = 0;
    for (const auto value : values) {
        if (!std::isfinite(value) || value <= -1) { throw std::invalid_argument("invalid account return"); }
        sum += value;
    }
    const auto mean = sum / values.size();
    long double centered = 0;
    for (const auto value : values) { centered += (value - mean) * (value - mean); }
    const double cer = static_cast<double>(252 * (mean - 1.5L * centered / (values.size() - 1)));
    if (!std::isfinite(cer)) { throw std::overflow_error("CER exceeds finite range"); }
    return cer;
}
DailyRiskStudy::DailyRiskStudy(std::size_t replicates) : replicates_{replicates} {
    if (replicates < 2) { throw std::invalid_argument("bootstrap requires at least two replicates"); }
}
const RiskAccountCase& risk_account_case(const DailyRiskResult& result, std::string_view name, int delay, double cost) {
    const auto found = std::find_if(result.accounts.begin(), result.accounts.end(), [&](const auto& a) {
        return a.account == name && a.delay_minutes == delay && a.cost_bps == cost;
    });
    if (found == result.accounts.end()) { throw std::invalid_argument("unknown risk account case"); }
    return *found;
}
DailyRiskResult DailyRiskStudy::run(const RiskStudyInputs& inputs, const SessionBatch::Progress& progress) const {
    validate(inputs, replicates_);
    DailyRiskResult result{};
    result.input_evidence = inputs.input_evidence;
    result.synthetic = inputs.synthetic; result.protocol_sha256 = inputs.protocol_sha256;
    result.development_manifest_sha256 = inputs.development_manifest_sha256;
    result.acquisition_manifest_sha256 = inputs.acquisition_manifest_sha256; result.issuer_reference_sha256 = inputs.issuer_reference_sha256;
    if (progress) { progress("Calculating dividend-aware daily measurements and prior-session risk estimates"); }
    double previous = inputs.boundary_close;
    for (const auto* sessions : {&inputs.calibration, &inputs.development}) {
        for (const auto& session : *sessions) {
            result.measurements.push_back(measure_daily_risk(session, previous, dividend_on(inputs, session.calendar().date())));
            previous = session.close();
        }
    }
    long double calibration_sum = 0;
    for (std::size_t i = 0; i < inputs.calibration.size(); ++i) { calibration_sum += result.measurements[i].daily2; }
    result.calibration_daily2 = static_cast<double>(calibration_sum / inputs.calibration.size());
    result.static_weight = risk_budget_weight(result.calibration_daily2);
    for (const auto delay : risk_rules::delays) {
        for (std::size_t i = 0; i < inputs.development.size(); ++i) {
            const auto end = inputs.calibration.size() + i;
            auto estimate = LaggedRiskEstimator{}.estimate(std::span{result.measurements}.subspan(end - risk_rules::history, risk_rules::history));
            auto missing = std::move(estimate.missing_dates);
            const auto& previous_session = i ? inputs.development[i-1] : inputs.calibration.back();
            const double hd = estimate.daily;
            const auto hi = estimate.intraday;
            RiskForecast f{inputs.development[i].calendar().date(), delay,
                format_utc_seconds(previous_session.calendar().close_utc() + minutes{5}),
                result.measurements[end - risk_rules::history].date, result.measurements[end - 1].date,
                hd, hi, hi.value_or(hd), risk_budget_weight(hi.value_or(hd)), risk_budget_weight(hd), std::move(missing), {}, {}, {}, {}, {}};
            if (i + 1 < inputs.development.size()) {
                const auto& next = inputs.development[i+1];
                const auto outcome = measure_holding_risk(inputs.development[i], next, dividend_on(inputs, next.calendar().date()), delay);
                f.future_endpoint2 = outcome.endpoint2; f.future_rv5 = outcome.rv5;
                if (hi && outcome.rv5) {
                    const auto loss_increment = [&](double y) { return (std::log(hd) - std::log(*hi)) + y * (1 / hd - 1 / *hi); };
                    f.qlike_increment = loss_increment(*outcome.rv5);
                    f.squared_error_increment = std::pow(*outcome.rv5 - hd, 2) - std::pow(*outcome.rv5 - *hi, 2);
                    f.endpoint_qlike_increment = loss_increment(outcome.endpoint2);
                    if (!std::isfinite(*f.qlike_increment) || !std::isfinite(*f.squared_error_increment) || !std::isfinite(*f.endpoint_qlike_increment)) {
                        throw std::overflow_error("forecast loss exceeds finite range");
                    }
                }
            }
            result.forecasts.push_back(std::move(f));
        }
    }
    if (progress) { progress("Replaying four comparison accounts across forty cost and delay cases"); }
    for (const auto delay : risk_rules::delays) {
        for (const auto cost : risk_rules::costs) {
            for (const auto name : risk_rules::accounts) { result.accounts.push_back(run_account(inputs, result, std::string{name}, delay, cost)); }
        }
    }
    for (const auto& account : result.accounts) {
        for (const auto& period : {std::string{"all"}, std::string{"2022"}, std::string{"2023"}}) {
            result.statistics.push_back(statistics(account, period));
        }
    }
    for (const auto delay : risk_rules::delays) {
        long double q = 0, mse = 0, endpoint = 0; std::size_t count = 0;
        for (const auto& f : result.forecasts) {
            if (f.delay_minutes != delay || !f.qlike_increment) { continue; }
            q += *f.qlike_increment; mse += *f.squared_error_increment; endpoint += *f.endpoint_qlike_increment; ++count;
        }
        auto mean = [&](long double total) -> std::optional<double> { return count ? std::optional<double>{static_cast<double>(total / count)} : std::nullopt; };
        result.forecast_statistics.push_back({delay, count, mean(q), mean(mse), mean(endpoint)});
    }
    const auto& primary_path = risk_account_case(result, "intraday_risk", 1, 1);
    std::map<std::string, std::vector<std::string>> months;
    std::vector<WinningContribution> contributions;
    for (const auto& d : primary_path.days) { months[d.date.substr(0, 7)].push_back(d.date); contributions.push_back({d.date, d.net_return}); }
    for (auto& [month, dates] : months) { result.omissions.push_back(omission(result, "month", month, std::move(dates))); }
    const auto concentration = describe_return_concentration(contributions);
    std::vector<std::string> winners;
    for (const auto& winner : concentration.largest_five_winners) {
        winners.push_back(winner.date);
        result.omissions.push_back(omission(result, "single_largest_winner", winner.date, {winner.date}));
    }
    if (!winners.empty()) { result.omissions.push_back(omission(result, "five_largest_winners", "primary_intraday_account", std::move(winners))); }
    result.primary = contrast(result, 1);
    result.gates.push_back({"positive_primary_CER_over_daily", result.primary.cer_over_daily > 0});
    result.gates.push_back({"positive_primary_CER_over_static", result.primary.cer_over_static > 0});
    result.gates.push_back({"positive_primary_QLIKE_increment", result.primary.qlike_increment && *result.primary.qlike_increment > 0});
    for (const auto block : {std::size_t{20}, std::size_t{40}}) {
        if (progress) { progress("Paired full-calendar moving-block uncertainty | block " + std::to_string(block)); }
        result.uncertainty.push_back(bootstrap(result, block, replicates_, block == 20 ? 2026102220ULL : 2026102240ULL));
        const auto& b = result.uncertainty.back();
        result.gates.push_back({"positive_lower_CER_daily_block_" + std::to_string(block), b.cer_over_daily.lower > 0});
        result.gates.push_back({"positive_lower_CER_static_block_" + std::to_string(block), b.cer_over_static.lower > 0});
        result.gates.push_back({"positive_lower_QLIKE_block_" + std::to_string(block), b.qlike_increment && b.qlike_increment->lower > 0});
    }
    for (const auto& year : {std::string{"2022"}, std::string{"2023"}}) {
        const auto c = contrast(result, 1, year);
        result.gates.push_back({"positive_CER_over_daily_" + year, c.cer_over_daily > 0});
        result.gates.push_back({"positive_CER_over_static_" + year, c.cer_over_static > 0});
    }
    const auto stress = contrast(result, 2);
    result.gates.push_back({"positive_CER_over_daily_at_2bp", stress.cer_over_daily > 0});
    result.gates.push_back({"positive_CER_over_static_at_2bp", stress.cer_over_static > 0});
    return result;
}
} // namespace trading
