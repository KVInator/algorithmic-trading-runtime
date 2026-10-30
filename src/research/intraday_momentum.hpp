#pragma once

#include "alpaca_bar_request.hpp"
#include "minute_bar.hpp"
#include "research_session.hpp"

#include <chrono>
#include <optional>
#include <span>

namespace trading {

struct MomentumDecision {
    std::chrono::sys_seconds at;
    double signal_return;
    bool enter_long;
};

// SPY-IM-01 assumes an independently verified 09:30-16:00 New York session.
// The constructor checks the 390-minute window and possible UTC opening clocks;
// it does not perform a calendar lookup or choose the date's UTC offset.
class IntradayMomentumStrategy {
  public:
    explicit IntradayMomentumStrategy(HistoricalBarRequest session,
                                      ResearchInputMode mode = ResearchInputMode::legacy_complete,
                                      int hold_minutes = 25, PanelEvaluationAccess access = {});

    [[nodiscard]] MomentumDecision decide(std::span<const MinuteBar> completed_bars,
                                          std::chrono::sys_seconds at) const;
    [[nodiscard]] const HistoricalBarRequest &session() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds entry_decision_time() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds exit_decision_time() const noexcept;

  private:
    HistoricalBarRequest session_;
    ResearchInputMode mode_;
    int hold_minutes_;
};

struct LongPriceReturn {
    double buy_after_cost;
    double sell_after_cost;
    double gross_return;
    double net_return;
};

// Continuous-notional research return; cost is charged on both sides.
[[nodiscard]] LongPriceReturn evaluate_long_prices(double entry_reference, double exit_reference,
                                                   double cost_bps_per_side);

struct ScheduledRoundTrip {
    std::chrono::sys_seconds entry_decision;
    std::chrono::sys_seconds entry_reference_time;
    std::chrono::sys_seconds exit_decision;
    std::chrono::sys_seconds exit_reference_time;
    double entry_reference_price;
    double exit_reference_price;
    LongPriceReturn returns;
};

struct MomentumReplay {
    MomentumDecision decision;
    std::optional<ScheduledRoundTrip> strategy_trade;
    ScheduledRoundTrip always_long_control;

    [[nodiscard]] double strategy_gross_return() const noexcept;
    [[nodiscard]] double strategy_net_return() const noexcept;
};

// Deterministic scheduled-price replay, not an order book or brokerage simulator.
class DelayedBarReplay {
  public:
    explicit DelayedBarReplay(std::chrono::minutes delay = std::chrono::minutes{1},
                              double cost_bps_per_side = 1.0);
    [[nodiscard]] MomentumReplay run(const IntradayMomentumStrategy &strategy,
                                     std::span<const MinuteBar> bars) const;
    [[nodiscard]] std::chrono::minutes delay() const noexcept;
    [[nodiscard]] double cost_bps_per_side() const noexcept;

  private:
    std::chrono::minutes delay_;
    double cost_bps_per_side_;
};

} // namespace trading
