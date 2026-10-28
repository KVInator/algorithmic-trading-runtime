#include "intraday_continuation.hpp"

#include "market_calendar.hpp"
#include "session_download.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

void validate_session(const HistoricalBarRequest& session, ResearchInputMode mode) {
    if (mode == ResearchInputMode::registered_panel) { require_panel_regular_session(session); return; }
    const auto date = format_utc_seconds(session.start()).substr(0, 10);
    const CalendarSession expected{date, "09:30", "16:00"};
    if (session.symbol() != "SPY" || session.start() != expected.open_utc() ||
        session.end_exclusive() != expected.close_utc()) {
        throw std::invalid_argument("SPY-TOD-01 requires a calendar-supplied full SPY regular session");
    }
}

double price_move(double open, double close) {
    const double result = (close - open) / open;
    if (!std::isfinite(result)) {
        throw std::overflow_error("intraday price move exceeds the finite range");
    }
    return result;
}

void multiply_wealth(double& wealth, double trade_return) {
    wealth *= 1.0 + trade_return;
    if (!std::isfinite(wealth) || wealth <= 0.0) {
        throw std::overflow_error("compounded session wealth is outside the positive finite range");
    }
}

} // namespace

IntradayContinuationHistory::IntradayContinuationHistory(ResearchInputMode mode) : mode_{mode} {}

void IntradayContinuationHistory::append(const HistoricalBarRequest& session,
                                         std::span<const MinuteBar> bars) {
    validate_session(session, mode_);
    if (!symbol_.empty() && symbol_ != session.symbol()) { throw std::invalid_argument("continuation history cannot mix instruments"); }
    if (!sessions_.empty() && session.start() <= sessions_.back().end) {
        throw std::invalid_argument("history sessions must be strictly chronological and disjoint");
    }
    if (!audit_session(session, bars).complete()) {
        throw std::invalid_argument("feature history requires complete session minute coverage");
    }
    SessionProfile profile{session.start(), session.end_exclusive(), {}, {}};
    double volume = 0.0;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        volume += bars[index].volume();
        // audit_session has already checked finite total nonnegative volume.
        profile.absolute_moves[index] = std::abs(price_move(
            bars.front().prices().open, bars[index].prices().close));
        profile.cumulative_volumes[index] = volume;
    }
    symbol_ = session.symbol();
    sessions_.push_back(std::move(profile));
    if (sessions_.size() > continuation_history_sessions) {
        sessions_.pop_front();
    }
}

bool IntradayContinuationHistory::ready() const noexcept {
    return sessions_.size() == continuation_history_sessions;
}

std::size_t IntradayContinuationHistory::size() const noexcept { return sessions_.size(); }

IntradayContinuationFeatures IntradayContinuationHistory::measure(
    const HistoricalBarRequest& current_session,
    std::span<const MinuteBar> completed_bars,
    std::chrono::sys_seconds at) const {
    validate_session(current_session, mode_);
    if (symbol_ != current_session.symbol()) { throw std::invalid_argument("continuation history belongs to another instrument"); }
    if (!ready()) {
        throw std::logic_error("intraday features require 20 complete earlier eligible sessions");
    }
    if (sessions_.back().end >= current_session.start()) {
        throw std::invalid_argument("feature history must precede the entire current session");
    }
    if (at <= current_session.start() || at > current_session.end_exclusive() ||
        at != std::chrono::floor<std::chrono::minutes>(at)) {
        throw std::invalid_argument("feature time must follow a completed minute in the current session");
    }
    const HistoricalBarRequest prefix{current_session.symbol(), current_session.start(), at,
                                      current_session.page_size()};
    const auto audit = audit_session(prefix, completed_bars);
    if (!audit.complete()) {
        throw std::invalid_argument("features require every completed minute before the decision");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
        at - current_session.start()).count();
    const auto index = static_cast<std::size_t>(elapsed - 1);
    double typical_move = 0.0;
    std::array<double, continuation_history_sessions> volumes{};
    for (std::size_t session_index = 0; session_index < sessions_.size(); ++session_index) {
        // Nonnegative online means avoid a potentially overflowing sum and
        // preserve exact equality when every historical move is identical.
        typical_move += (sessions_[session_index].absolute_moves[index] - typical_move) /
                        static_cast<double>(session_index + 1);
        volumes[session_index] = sessions_[session_index].cumulative_volumes[index];
    }
    if (!std::isfinite(typical_move)) {
        throw std::overflow_error("historical price-move mean exceeds the finite range");
    }
    std::sort(volumes.begin(), volumes.end());
    constexpr auto middle = continuation_history_sessions / 2;
    const double median = volumes[middle - 1] + (volumes[middle] - volumes[middle - 1]) / 2.0;
    if (median <= 0.0) {
        throw std::domain_error("historical same-clock median volume is zero; relative volume is undefined");
    }
    const double relative_volume = audit.total_volume_shares / median;
    if (!std::isfinite(relative_volume)) {
        throw std::overflow_error("relative volume exceeds the finite range");
    }
    return {at, sessions_.front().start, sessions_.back().start,
            price_move(completed_bars.front().prices().open, completed_bars.back().prices().close),
            typical_move, audit.total_volume_shares, median, relative_volume};
}

std::string_view continuation_action_name(ContinuationAction action) {
    switch (action) {
    case ContinuationAction::stay_cash: return "cash";
    case ContinuationAction::enter_long: return "enter";
    case ContinuationAction::hold_long: return "hold";
    case ContinuationAction::exit_long: return "exit";
    }
    throw std::invalid_argument("unknown continuation action");
}

std::string_view continuation_protocol_sha256() noexcept {
    return TRADING_CONTINUATION_PROTOCOL_SHA256;
}

IntradayContinuationStrategy::IntradayContinuationStrategy(
    HistoricalBarRequest session, ContinuationVariant variant, ResearchInputMode mode)
    : session_{std::move(session)}, variant_{variant}, mode_{mode} {
    validate_session(session_, mode_);
    if (variant_ != ContinuationVariant::price_only && variant_ != ContinuationVariant::volume_conditioned) {
        throw std::invalid_argument("unknown continuation variant");
    }
}

const HistoricalBarRequest& IntradayContinuationStrategy::session() const noexcept { return session_; }
ContinuationVariant IntradayContinuationStrategy::variant() const noexcept { return variant_; }

std::array<std::chrono::sys_seconds, 12> IntradayContinuationStrategy::decision_times() const {
    std::array<std::chrono::sys_seconds, 12> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = session_.start() + std::chrono::minutes{
            30 * static_cast<int>(index + 1)};
    }
    return result;
}

std::chrono::sys_seconds IntradayContinuationStrategy::liquidation_time() const noexcept {
    return session_.start() + std::chrono::minutes{385};
}

IntradayContinuationDecision IntradayContinuationStrategy::decide(
    const IntradayContinuationHistory& history,
    std::span<const MinuteBar> completed_bars,
    std::chrono::sys_seconds at,
    bool currently_long) const {
    const auto clocks = decision_times();
    if (std::find(clocks.begin(), clocks.end(), at) == clocks.end()) {
        throw std::invalid_argument("continuation decisions require a registered half-hour clock");
    }
    const auto unavailable = [&] { return IntradayContinuationDecision{at,
        currently_long ? ContinuationAction::exit_long : ContinuationAction::stay_cash, std::nullopt}; };
    if (mode_ == ResearchInputMode::registered_panel) {
        if (!audit_session(HistoricalBarRequest{session_.symbol(), session_.start(), at}, completed_bars).complete() || !history.ready()) {
            return unavailable();
        }
    }
    IntradayContinuationFeatures features{};
    try { features = history.measure(session_, completed_bars, at); }
    catch (const std::domain_error&) {
        if (mode_ != ResearchInputMode::registered_panel) { throw; }
        return unavailable();
    }
    return {at, action(features, variant_, currently_long), features};
}
ContinuationAction IntradayContinuationStrategy::action(const IntradayContinuationFeatures& features,
    ContinuationVariant variant, bool currently_long) {
    if (variant != ContinuationVariant::price_only && variant != ContinuationVariant::volume_conditioned) {
        throw std::invalid_argument("unknown continuation variant");
    }
    const bool price_condition = features.move > features.typical_absolute_move;
    if (currently_long) { return price_condition ? ContinuationAction::hold_long : ContinuationAction::exit_long; }
    return price_condition && (variant == ContinuationVariant::price_only || features.relative_volume > 1.0)
        ? ContinuationAction::enter_long : ContinuationAction::stay_cash;
}

IntradayContinuationReplayEngine::IntradayContinuationReplayEngine(
    std::chrono::minutes delay, double cost_bps_per_side)
    : delay_{delay}, cost_bps_per_side_{cost_bps_per_side} {
    if (delay_ != std::chrono::minutes{1} && delay_ != std::chrono::minutes{2}) {
        throw std::invalid_argument("continuation replay requires a one- or two-minute delay");
    }
    // Reuse the authoritative price/cost validation without a second cost rule.
    static_cast<void>(evaluate_long_prices(1.0, 1.0, cost_bps_per_side_));
}

IntradayContinuationReplay IntradayContinuationReplayEngine::run(
    const IntradayContinuationStrategy& strategy,
    const IntradayContinuationHistory& history,
    std::span<const MinuteBar> bars) const {
    if (!audit_session(strategy.session(), bars).complete()) {
        throw std::invalid_argument("continuation replay requires a complete regular session");
    }
    const auto clocks = strategy.decision_times();
    const auto reference_price = [&](std::chrono::sys_seconds decision_time) {
        const auto index = std::chrono::duration_cast<std::chrono::minutes>(
            decision_time + delay_ - strategy.session().start()).count();
        return bars[static_cast<std::size_t>(index)].prices().open;
    };
    const auto make_trade = [&](std::chrono::sys_seconds entry, std::chrono::sys_seconds exit) {
        const double entry_price = reference_price(entry);
        const double exit_price = reference_price(exit);
        return ScheduledRoundTrip{entry, entry + delay_, exit, exit + delay_, entry_price,
                                  exit_price, evaluate_long_prices(entry_price, exit_price,
                                                                  cost_bps_per_side_)};
    };
    IntradayContinuationReplay result{{}, {}, make_trade(clocks.front(), strategy.liquidation_time()),
                                     0.0, 0.0, std::chrono::minutes{0}};
    std::optional<std::chrono::sys_seconds> entry;
    const auto close_trade = [&](std::chrono::sys_seconds exit) {
        if (!entry) {
            throw std::logic_error("exit decision has no open continuation position");
        }
        auto trade = make_trade(*entry, exit);
        result.minutes_in_market += std::chrono::duration_cast<std::chrono::minutes>(
            trade.exit_reference_time - trade.entry_reference_time);
        result.trades.push_back(std::move(trade));
        entry.reset();
    };
    for (const auto at : clocks) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
            at - strategy.session().start()).count();
        auto decision = strategy.decide(history, bars.first(static_cast<std::size_t>(elapsed)),
                                        at, entry.has_value());
        if (decision.action == ContinuationAction::enter_long) {
            entry = at;
        } else if (decision.action == ContinuationAction::exit_long) {
            close_trade(at);
        }
        result.decisions.push_back(std::move(decision));
    }
    const bool had_position = entry.has_value();
    if (entry) { close_trade(strategy.liquidation_time()); }
    result.decisions.push_back({strategy.liquidation_time(), had_position ? ContinuationAction::exit_long :
        ContinuationAction::stay_cash, std::nullopt});
    double gross_wealth = 1.0;
    double net_wealth = 1.0;
    for (const auto& trade : result.trades) {
        multiply_wealth(gross_wealth, trade.returns.gross_return);
        multiply_wealth(net_wealth, trade.returns.net_return);
    }
    result.strategy_gross_return = gross_wealth - 1.0;
    result.strategy_net_return = net_wealth - 1.0;
    return result;
}

} // namespace trading
