#pragma once

#include "intraday_momentum.hpp"
#include "strategy_order_adapters.hpp"

namespace trading::execution {

// Explicit whole-share execution policy for the existing signal. Constructed
// diagnostics only; this does not revise the frozen continuous-notional study.
class MomentumOrderAdapter {
public:
    MomentumOrderAdapter(IntradayMomentumStrategy strategy, Shares long_target_shares,
                         Cents fee_ceiling_cents, std::int64_t quote_age_seconds = 2,
                         std::int64_t dispatch_window_seconds = 30);
    [[nodiscard]] TargetIntent entry(std::span<const MinuteBar> completed, const TopOfBook& quote) const;
    [[nodiscard]] TargetIntent liquidation(const TopOfBook& quote) const;
    [[nodiscard]] const IntradayMomentumStrategy& strategy() const noexcept;

private:
    [[nodiscard]] TargetIntent target(std::chrono::sys_seconds at, bool enter_long,
                                      const TopOfBook& quote) const;
    IntradayMomentumStrategy strategy_;
    WholeShareTargetPolicy policy_;
};

} // namespace trading::execution
