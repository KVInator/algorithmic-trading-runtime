#include "panel_replay.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trading {
namespace {
constexpr double primary_cost = 1;
constexpr auto primary_delay = std::chrono::minutes{1};

class PolicyPath {
public:
    PolicyPath(std::string name, const HistoricalBarRequest& request, std::span<const MinuteBar> bars)
        : result_{std::move(name), {}, {}, true, 0, 0}, request_(request), view_(request, bars) {}
    void target(std::chrono::sys_seconds at, bool desired, std::string reason,
        std::optional<IntradayContinuationFeatures> features = {}) {
        if (!result_.execution_resolved) {
            result_.decisions.push_back({at, {}, "unresolved_prior_execution", {}}); return;
        }
        result_.decisions.push_back({at, desired, std::move(reason), std::move(features)});
        if (desired == entry_.has_value()) { return; }
        const auto minute = std::chrono::duration_cast<std::chrono::minutes>(at + primary_delay - request_.start()).count();
        const auto* reference = view_.at(static_cast<std::size_t>(minute));
        if (reference == nullptr) { result_.execution_resolved = false; return; }
        if (desired) { entry_ = std::pair{at, reference->prices().open}; }
        else {
            const auto values = evaluate_long_prices(entry_->second, reference->prices().open, primary_cost);
            result_.trades.push_back({entry_->first, entry_->first + primary_delay, at, at + primary_delay,
                entry_->second, reference->prices().open, values});
            gross_ *= 1 + values.gross_return; net_ *= 1 + values.net_return;
            entry_.reset();
        }
    }
    [[nodiscard]] bool long_position() const { return entry_.has_value(); }
    [[nodiscard]] bool resolved() const { return result_.execution_resolved; }
    PanelPolicyReplay finish() {
        if (entry_ && result_.execution_resolved) { throw std::logic_error("intraday path was not liquidated"); }
        if (!std::isfinite(gross_) || !std::isfinite(net_) || gross_ <= 0 || net_ <= 0) { throw std::overflow_error("intraday replay wealth overflow"); }
        result_.gross = gross_ - 1; result_.net = net_ - 1;
        return std::move(result_);
    }
private:
    PanelPolicyReplay result_;
    HistoricalBarRequest request_;
    ResearchSessionView view_;
    std::optional<std::pair<std::chrono::sys_seconds, double>> entry_;
    double gross_{1}, net_{1};
};
PanelRiskPrediction predict(const std::deque<DailyRiskMeasurement>& history, const std::string& cutoff,
    std::optional<double>& fixed, const PanelReplayParameters& parameters) {
    PanelRiskPrediction out{{}, {}, cutoff, "daily_history_unavailable", {}, {}, {}, fixed, history.size()};
    if (history.size() < 252) { return out; }
    std::vector<DailyRiskMeasurement> prior(history.end() - static_cast<std::ptrdiff_t>(parameters.risk_sessions), history.end());
    out.history_first = prior.front().date; out.history_last = prior.back().date;
    const auto estimate = LaggedRiskEstimator{parameters.risk_sessions, parameters.risk_grid_minutes}.estimate(prior);
    if (estimate.daily <= 0 || (estimate.intraday && *estimate.intraday <= 0)) { out.reason = "zero_risk_estimate"; return out; }
    out.estimate = estimate;
    out.daily_weight = risk_budget_weight(estimate.daily);
    out.intraday_weight = risk_budget_weight(estimate.intraday.value_or(estimate.daily));
    if (!fixed) {
        long double sum = 0;
        for (const auto& row : history) { sum += row.daily2; }
        if (sum > 0) { fixed = risk_budget_weight(static_cast<double>(sum / history.size())); }
    }
    out.static_weight = fixed;
    out.reason = estimate.intraday ? "prior_rv5" : "prior_daily_fallback";
    return out;
}
void require_equal(double a, double b) {
    if (std::abs(a - b) > 1e-12 * std::max({1.0, std::abs(a), std::abs(b)})) { throw std::runtime_error("original-reference numerical parity failed"); }
}
} // namespace

void PanelReplayParameters::validate() const {
    if (family != "all" && family != "momentum" && family != "continuation" && family != "reversion" && family != "risk") {
        throw std::invalid_argument("unknown replay family");
    }
    if (momentum_hold_minutes != 10 && momentum_hold_minutes != 20 && momentum_hold_minutes != 25) {
        throw std::invalid_argument("unsupported momentum duration");
    }
    reversion.validate(ResearchInputMode::registered_panel);
    static_cast<void>(LaggedRiskEstimator{risk_sessions, risk_grid_minutes});
    static_cast<void>(IntradayContinuationHistory{ResearchInputMode::registered_panel, continuation_sessions});
}
PanelReplayState::PanelReplayState(std::string symbol, PanelInputPolicy policy, PanelReplayParameters parameters)
    : parameters_(std::move(parameters)), symbol_(std::move(symbol)), policy_(std::move(policy)),
      continuation_(ResearchInputMode::registered_panel, parameters_.continuation_sessions),
      reversion_(ResearchInputMode::registered_panel, parameters_.reversion) {
    parameters_.validate();
}
PanelReplayDay PanelReplayState::advance(const HistoricalBarRequest& request, std::span<const MinuteBar> bars, double dividend, const std::optional<MinuteBar>& recovered) {
    const auto date = format_utc_seconds(request.start()).substr(0, 10);
    if (date >= "2024-01-01" || request.symbol() != symbol_ || (!previous_date_.empty() && date <= previous_date_) ||
        !std::isfinite(dividend) || dividend < 0) { throw std::invalid_argument("panel state requires ordered pre-2024 sessions of one instrument"); }
    if (!previous_date_.empty()) {
        const auto next = parse_session_date(date), prior = parse_session_date(previous_date_);
        if (next - prior > std::chrono::days{14}) { throw std::invalid_argument("panel state cannot skip calendar history"); }
        for (auto cursor = prior + std::chrono::days{1}; cursor < next; cursor += std::chrono::days{1}) {
            const auto between = format_utc_seconds(cursor).substr(0, 10);
            if (!extension_calendar(CalendarRange{between, between}).empty()) { throw std::invalid_argument("panel state skipped an exchange-session slot"); }
        }
    }
    const auto dates = extension_calendar(CalendarRange{date, date});
    if (dates.size() != 1 || dates.front().open_utc() != request.start() || dates.front().close_utc() != request.end_exclusive()) {
        throw std::invalid_argument("panel replay clock differs from the exchange calendar");
    }
    const auto& calendar = dates.front();
    const auto audit = audit_session(request, bars);
    const auto availability = policy_.evaluate(calendar, symbol_, audit.missing_minutes, static_cast<std::size_t>(parameters_.reversion.observation_minutes));
    const bool diagnostic = symbol_ == "SPY" && date == "2023-01-03";
    PanelReplayDay out{symbol_, date, date > "2016-12-31", false, availability, {}, {}, {},
        predict(risk_history_, previous_close_cutoff_, static_weight_, parameters_), {}};
    const ResearchSessionView observed{request, bars};
    if (calendar.regular()) {
        const IntradayMomentumStrategy momentum{request, ResearchInputMode::registered_panel, parameters_.momentum_hold_minutes};
        if (!diagnostic && availability.momentum_features) {
            out.momentum = momentum.decide(observed.before(momentum.entry_decision_time()), momentum.entry_decision_time());
        }
        for (const bool control : {false, true}) {
            PolicyPath path{control ? "momentum_always_long" : "momentum", request, bars};
            const bool target = out.momentum && (control || out.momentum->enter_long);
            path.target(momentum.entry_decision_time(), target, out.momentum ? "observed_signal" : "feature_unavailable");
            path.target(momentum.exit_decision_time(), false, "scheduled_liquidation");
            out.policies.push_back(path.finish());
        }
        for (const auto variant : {ContinuationVariant::price_only, ContinuationVariant::volume_conditioned}) {
            const IntradayContinuationStrategy strategy{request, variant, ResearchInputMode::registered_panel};
            PolicyPath path{variant == ContinuationVariant::price_only ? "continuation_price" : "continuation_volume", request, bars};
            for (const auto at : strategy.decision_times()) {
                if (!path.resolved()) { path.target(at, false, "unresolved_prior_execution"); continue; }
                const auto decision = diagnostic ? IntradayContinuationDecision{at, path.long_position() ? ContinuationAction::exit_long : ContinuationAction::stay_cash, {}} :
                    strategy.decide(continuation_, observed.before(at), at, path.long_position());
                const bool target = decision.action == ContinuationAction::enter_long || decision.action == ContinuationAction::hold_long;
                path.target(at, target, decision.features ? std::string{continuation_action_name(decision.action)} : "feature_unavailable", decision.features);
            }
            path.target(strategy.liquidation_time(), false, "scheduled_liquidation");
            out.policies.push_back(path.finish());
        }
        if (!diagnostic && reversion_.ready()) {
            out.reversion = ReversionStrategy{ResearchInputMode::registered_panel, parameters_.reversion}.decide(request,
                observed.before(request.start() + std::chrono::minutes{150}), reversion_);
        }
        for (std::size_t i = 0; i < 4; ++i) {
            PolicyPath path{"reversion_" + std::string{name(static_cast<ReversionPolicy>(i))}, request, bars};
            path.target(request.start() + std::chrono::minutes{150}, out.reversion && out.reversion->actions[i].enter,
                out.reversion ? out.reversion->actions[i].reason : "history_or_feature_unavailable");
            path.target(request.start() + std::chrono::minutes{150 + parameters_.reversion.hold_minutes}, false, "scheduled_liquidation");
            out.policies.push_back(path.finish());
        }
    }
    // Commit only after all current decisions, including the morning forecast.
    std::vector<MinuteBar> risk_bars(bars.begin(), bars.end());
    if (recovered) {
        if (recovered->symbol() != symbol_ || recovered->start() != request.end_exclusive() - std::chrono::minutes{1} || observed.at(request.expected_minutes() - 1) != nullptr) {
            throw std::invalid_argument("invalid or duplicate recovered daily close");
        }
        risk_bars.push_back(*recovered);
    }
    const RiskSessionReferences references{calendar, std::move(risk_bars), symbol_, ResearchInputMode::registered_panel};
    const auto close = references.observed_close();
    if (previous_close_ && close) { out.measurement = measure_daily_risk(references, *previous_close_, dividend); }
    if (out.measurement) {
        risk_history_.push_back(*out.measurement);
        if (risk_history_.size() > 252) { risk_history_.pop_front(); }
    } else { risk_history_.clear(); }
    previous_close_ = close;
    previous_close_cutoff_ = format_utc_seconds(calendar.close_utc() + std::chrono::minutes{5});
    if (calendar.regular()) {
        static_cast<void>(reversion_.complete(request, bars, request.end_exclusive(), !diagnostic));
        if (audit.complete() && !diagnostic) { continuation_.append(request, bars); }
    }
    if (parameters_.family != "all") {
        const auto belongs = [&](const PanelPolicyReplay& path) { return path.policy.starts_with(parameters_.family + "_") || path.policy == parameters_.family; };
        std::erase_if(out.policies, [&](const auto& path) { return !belongs(path); });
    }
    previous_date_ = date;
    return out;
}

nlohmann::json verify_panel_reference_parity(std::span<const SessionDownload> sessions, const RiskStudyInputs* risk) {
    using Json = nlohmann::json;
    if (sessions.size() < 21) { throw std::invalid_argument("reference parity requires initialized histories and at least 21 sessions"); }
    IntradayContinuationHistory old_cont, new_cont{ResearchInputMode::registered_panel};
    ReversionHistory old_rev, new_rev{ResearchInputMode::registered_panel};
    std::size_t momentum_dates = 0, continuation_dates = 0, reversion_dates = 0, risk_dates = 0;
    for (const auto& session : sessions) {
        const IntradayMomentumStrategy old_m{session.request}, new_m{session.request, ResearchInputMode::registered_panel};
        const auto old_mr = DelayedBarReplay{}.run(old_m, session.bars), new_mr = DelayedBarReplay{}.run(new_m, session.bars);
        require_equal(old_mr.decision.signal_return, new_mr.decision.signal_return);
        if (old_mr.decision.enter_long != new_mr.decision.enter_long) { throw std::runtime_error("momentum reference action changed"); }
        require_equal(old_mr.strategy_net_return(), new_mr.strategy_net_return()); ++momentum_dates;
        if (old_cont.ready()) {
            for (const auto variant : {ContinuationVariant::price_only, ContinuationVariant::volume_conditioned}) {
                const auto a = IntradayContinuationReplayEngine{}.run(IntradayContinuationStrategy{session.request, variant}, old_cont, session.bars);
                const auto b = IntradayContinuationReplayEngine{}.run(IntradayContinuationStrategy{session.request, variant, ResearchInputMode::registered_panel}, new_cont, session.bars);
                require_equal(a.strategy_net_return, b.strategy_net_return);
                if (a.trades.size() != b.trades.size()) { throw std::runtime_error("continuation reference trades changed"); }
                for (std::size_t i = 0; i < a.decisions.size(); ++i) {
                    if (a.decisions[i].action != b.decisions[i].action) { throw std::runtime_error("continuation reference action changed"); }
                }
            }
            ++continuation_dates;
        }
        if (old_rev.ready()) {
            const auto prefix = std::span{session.bars}.first(150);
            const auto a = ReversionStrategy{}.decide(session.request, prefix, old_rev);
            const auto b = ReversionStrategy{ResearchInputMode::registered_panel}.decide(session.request, prefix, new_rev);
            if (a.prior.complete != b.prior.complete || a.prior.fit.status != b.prior.fit.status || a.current.has_value() != b.current.has_value()) {
                throw std::runtime_error("reversion reference availability changed");
            }
            if (a.current && a.prior.complete) {
                for (std::size_t i = 0; i < 4; ++i) { if (a.actions[i].enter != b.actions[i].enter) { throw std::runtime_error("reversion reference action changed"); } }
                if (a.expected_move && b.expected_move) { require_equal(*a.expected_move, *b.expected_move); }
            }
            ++reversion_dates;
        }
        old_cont.append(session.request, session.bars); new_cont.append(session.request, session.bars);
        static_cast<void>(old_rev.complete(session.request, session.bars, session.request.end_exclusive()));
        static_cast<void>(new_rev.complete(session.request, session.bars, session.request.end_exclusive()));
    }
    if (risk != nullptr) {
        double previous = risk->boundary_close;
        std::vector<DailyRiskMeasurement> history;
        for (const auto* days : {&risk->calibration, &risk->development}) {
            for (const auto& day : *days) {
                double dividend = 0;
                for (const auto& action : risk->distributions) { if (action.ex_date == day.calendar().date()) { dividend += action.usd_per_share; } }
                const auto m = measure_daily_risk(day, previous, dividend);
                if (history.size() >= 20) {
                    const auto prior = std::span{history}.last(20);
                    const auto estimate = LaggedRiskEstimator{}.estimate(prior);
                    long double daily = 0, intraday = 0; bool complete = true;
                    for (const auto& row : prior) { daily += row.daily2; if (row.rv5) { intraday += *row.rv5; } else { complete = false; } }
                    require_equal(estimate.daily, static_cast<double>(daily / 20));
                    if (estimate.intraday.has_value() != complete) { throw std::runtime_error("risk reference fallback changed"); }
                    if (complete) { require_equal(*estimate.intraday, static_cast<double>(intraday / 20)); }
                    ++risk_dates;
                }
                history.push_back(m); previous = day.close();
            }
        }
    }
    return Json{{"passed", true}, {"momentum_dates", momentum_dates}, {"continuation_dates", continuation_dates},
        {"reversion_dates", reversion_dates}, {"risk_estimate_windows", risk_dates}, {"comparison", "same_original_inputs_and_histories"},
        {"tolerance", 1e-12}};
}
} // namespace trading
