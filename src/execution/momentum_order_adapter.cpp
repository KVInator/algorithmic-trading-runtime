#include "momentum_order_adapter.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace trading::execution {
MomentumOrderAdapter::MomentumOrderAdapter(IntradayMomentumStrategy strategy, Shares shares, Cents fee,
                                           std::int64_t age, std::int64_t window)
    : strategy_{std::move(strategy)}, long_target_{shares}, fee_ceiling_{fee}, quote_age_{age}, dispatch_window_{window} {
    if (shares <= 0 || fee < 0 || age < 0 || window < 0) { throw std::invalid_argument("invalid momentum execution policy"); }
}
TargetIntent MomentumOrderAdapter::target(std::chrono::sys_seconds at, Shares shares, const TopOfBook& quote) const {
    const auto seconds = at.time_since_epoch().count();
    if (quote.sequence == 0 || quote.at_seconds <= 0 || quote.at_seconds > seconds || seconds - quote.at_seconds > quote_age_ ||
        quote.bid_cents <= 0 || quote.ask_cents < quote.bid_cents || quote.bid_shares < 0 || quote.ask_shares < 0 ||
        fee_ceiling_ >= quote.bid_cents || seconds > std::numeric_limits<std::int64_t>::max() - dispatch_window_) {
        throw std::invalid_argument("momentum routing requires a current causal quote and bounded units");
    }
    return {"IM1-" + std::to_string(seconds), strategy_.session().symbol(), seconds, seconds + dispatch_window_, shares,
            quote.ask_cents, quote.bid_cents, fee_ceiling_};
}
TargetIntent MomentumOrderAdapter::entry(std::span<const MinuteBar> completed, const TopOfBook& quote) const {
    const auto decision = strategy_.decide(completed, strategy_.entry_decision_time());
    return target(decision.at, decision.enter_long ? long_target_ : 0, quote);
}
TargetIntent MomentumOrderAdapter::liquidation(const TopOfBook& quote) const { return target(strategy_.exit_decision_time(), 0, quote); }
const IntradayMomentumStrategy& MomentumOrderAdapter::strategy() const noexcept { return strategy_; }

} // namespace trading::execution
