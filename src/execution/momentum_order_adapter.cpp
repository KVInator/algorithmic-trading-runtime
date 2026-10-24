#include "momentum_order_adapter.hpp"

#include <utility>

namespace trading::execution {
MomentumOrderAdapter::MomentumOrderAdapter(IntradayMomentumStrategy strategy, Shares shares, Cents fee,
                                           std::int64_t age, std::int64_t window)
    : strategy_{std::move(strategy)}, policy_{shares, fee, age, window} {}

TargetIntent MomentumOrderAdapter::target(std::chrono::sys_seconds at, bool enter_long,
                                          const TopOfBook& quote) const {
    return policy_.make("IM1", strategy_.session().symbol(), at, enter_long, quote);
}

TargetIntent MomentumOrderAdapter::entry(std::span<const MinuteBar> completed, const TopOfBook& quote) const {
    const auto decision = strategy_.decide(completed, strategy_.entry_decision_time());
    return target(decision.at, decision.enter_long, quote);
}

TargetIntent MomentumOrderAdapter::liquidation(const TopOfBook& quote) const {
    return target(strategy_.exit_decision_time(), false, quote);
}

const IntradayMomentumStrategy& MomentumOrderAdapter::strategy() const noexcept {
    return strategy_;
}

} // namespace trading::execution
