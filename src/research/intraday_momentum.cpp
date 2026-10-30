#include "intraday_momentum.hpp"
#include "session_download.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

void validate_cost(double cost_bps_per_side) {
    if (!std::isfinite(cost_bps_per_side) || cost_bps_per_side < 0 || cost_bps_per_side >= 10000) {
        throw std::invalid_argument("one-way cost must be finite, non-negative and below 10000 basis points");
    }
}

double relative_return(double from, double to) {
    const double result = (to - from) / from;
    if (!std::isfinite(result)) {
        throw std::overflow_error("price return exceeds the finite double range");
    }
    return result;
}

} // namespace

IntradayMomentumStrategy::IntradayMomentumStrategy(HistoricalBarRequest session, ResearchInputMode mode, int hold_minutes)
    : session_{std::move(session)}, mode_{mode}, hold_minutes_{hold_minutes} {
    if ((hold_minutes != 10 && hold_minutes != 20 && hold_minutes != 25) ||
        (mode == ResearchInputMode::legacy_complete && hold_minutes != 25)) {
        throw std::invalid_argument("unsupported momentum holding duration");
    }
    if (mode_ == ResearchInputMode::registered_panel) { require_panel_regular_session(session_); return; }
    const auto day = std::chrono::floor<std::chrono::days>(session_.start());
    const auto utc_open = session_.start() - day;
    if (session_.symbol() != "SPY" || session_.expected_minutes() != 390 ||
        (utc_open != std::chrono::hours{13} + std::chrono::minutes{30} &&
         utc_open != std::chrono::hours{14} + std::chrono::minutes{30})) {
        throw std::invalid_argument("SPY-IM-01 requires SPY and a 390-minute regular-session UTC window");
    }
}

const HistoricalBarRequest& IntradayMomentumStrategy::session() const noexcept { return session_; }

std::chrono::sys_seconds IntradayMomentumStrategy::entry_decision_time() const noexcept {
    return session_.start() + std::chrono::minutes{360};
}

std::chrono::sys_seconds IntradayMomentumStrategy::exit_decision_time() const noexcept {
    return session_.start() + std::chrono::minutes{360 + hold_minutes_};
}

MomentumDecision IntradayMomentumStrategy::decide(std::span<const MinuteBar> completed_bars,
                                                  std::chrono::sys_seconds at) const {
    if (at != entry_decision_time()) {
        throw std::invalid_argument("momentum decision must occur at the scheduled 15:30 session time");
    }
    const HistoricalBarRequest history_window{session_.symbol(), session_.start(), at,
                                              session_.page_size()};
    if (mode_ == ResearchInputMode::legacy_complete && !audit_session(history_window, completed_bars).complete()) {
        throw std::invalid_argument("momentum decision requires every completed minute before 15:30");
    }
    const ResearchSessionView observed{history_window, completed_bars};
    const auto* opening = observed.at(0);
    const auto* signal_close = observed.at(359);
    if (opening == nullptr || signal_close == nullptr) { throw std::invalid_argument("momentum opening or decision-close feature is unavailable"); }
    const auto signal = relative_return(opening->prices().open, signal_close->prices().close);
    return {at, signal, signal > 0};
}

LongPriceReturn evaluate_long_prices(double entry_reference, double exit_reference,
                                      double cost_bps_per_side) {
    if (!std::isfinite(entry_reference) || !std::isfinite(exit_reference) ||
        entry_reference <= 0 || exit_reference <= 0) {
        throw std::invalid_argument("long price references must be finite and positive");
    }
    validate_cost(cost_bps_per_side);
    const double fraction = cost_bps_per_side / 10000.0;
    const double buy = entry_reference * (1.0 + fraction);
    const double sell = exit_reference * (1.0 - fraction);
    if (!std::isfinite(buy) || !std::isfinite(sell) || buy <= 0 || sell <= 0) {
        throw std::range_error("cost-adjusted prices are outside the positive finite range");
    }
    return {buy, sell, relative_return(entry_reference, exit_reference), relative_return(buy, sell)};
}

double MomentumReplay::strategy_gross_return() const noexcept {
    return strategy_trade ? strategy_trade->returns.gross_return : 0.0;
}

double MomentumReplay::strategy_net_return() const noexcept {
    return strategy_trade ? strategy_trade->returns.net_return : 0.0;
}

DelayedBarReplay::DelayedBarReplay(std::chrono::minutes delay, double cost_bps_per_side)
    : delay_{delay}, cost_bps_per_side_{cost_bps_per_side} {
    if (delay_ != std::chrono::minutes{1} && delay_ != std::chrono::minutes{2}) {
        throw std::invalid_argument("SPY-IM-01 replay supports the declared one- and two-minute delays");
    }
    validate_cost(cost_bps_per_side_);
}

std::chrono::minutes DelayedBarReplay::delay() const noexcept { return delay_; }
double DelayedBarReplay::cost_bps_per_side() const noexcept { return cost_bps_per_side_; }

MomentumReplay DelayedBarReplay::run(const IntradayMomentumStrategy& strategy,
                                     std::span<const MinuteBar> bars) const {
    if (!audit_session(strategy.session(), bars).complete()) {
        throw std::invalid_argument("replay requires complete regular-session minute coverage");
    }
    // Only the first 360 completed bars cross the strategy boundary. Later
    // reference prices belong to the replay, never to the signal calculation.
    const auto decision = strategy.decide(bars.first(360), strategy.entry_decision_time());
    const auto entry_time = strategy.entry_decision_time() + delay_;
    const auto exit_time = strategy.exit_decision_time() + delay_;
    const auto entry_index = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::minutes>(
        entry_time - strategy.session().start()).count());
    const auto exit_index = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::minutes>(
        exit_time - strategy.session().start()).count());
    const auto entry = bars[entry_index].prices().open;
    const auto exit = bars[exit_index].prices().open;
    const ScheduledRoundTrip control{
        decision.at, entry_time, strategy.exit_decision_time(), exit_time, entry, exit,
        evaluate_long_prices(entry, exit, cost_bps_per_side_)};
    std::optional<ScheduledRoundTrip> trade;
    if (decision.enter_long) {
        trade = control;
    }
    return {decision, std::move(trade), control};
}

} // namespace trading
