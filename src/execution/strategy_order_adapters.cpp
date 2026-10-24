#include "strategy_order_adapters.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace trading::execution {
WholeShareTargetPolicy::WholeShareTargetPolicy(Shares shares, Cents fee, std::int64_t age,
                                               std::int64_t window)
    : long_shares_{shares}, fee_ceiling_{fee}, quote_age_{age}, dispatch_window_{window} {
    if (shares <= 0 || fee < 0 || age < 0 || window < 0) {
        throw std::invalid_argument("invalid whole-share execution policy");
    }
}

TargetIntent WholeShareTargetPolicy::make(std::string_view identity, std::string_view symbol,
                                          std::chrono::sys_seconds at, bool long_position,
                                          const TopOfBook& quote) const {
    const auto seconds = at.time_since_epoch().count();
    if (identity.empty() || symbol.empty() || quote.sequence == 0 || quote.at_seconds <= 0 ||
        quote.at_seconds > seconds || seconds - quote.at_seconds > quote_age_ || quote.bid_cents <= 0 ||
        quote.ask_cents < quote.bid_cents || quote.bid_shares < 0 || quote.ask_shares < 0 ||
        fee_ceiling_ >= quote.bid_cents ||
        seconds > std::numeric_limits<std::int64_t>::max() - dispatch_window_) {
        throw std::invalid_argument("target requires a current causal quote and bounded execution units");
    }
    return {std::string{identity} + "-" + std::to_string(seconds),
            std::string{symbol},
            seconds,
            seconds + dispatch_window_,
            long_position ? long_shares_ : 0,
            quote.ask_cents,
            quote.bid_cents,
            fee_ceiling_};
}

ContinuationOrderAdapter::ContinuationOrderAdapter(IntradayContinuationStrategy strategy, Shares shares,
                                                   Cents fee, std::int64_t age, std::int64_t window)
    : strategy_{std::move(strategy)}, policy_{shares, fee, age, window} {}

std::string_view ContinuationOrderAdapter::identity() const noexcept {
    return strategy_.variant() == ContinuationVariant::price_only ? "TOD-P1" : "TOD-V1";
}

ContinuationOrderDecision ContinuationOrderAdapter::decide(const IntradayContinuationHistory& history,
                                                           std::span<const MinuteBar> completed,
                                                           std::chrono::sys_seconds at, Shares position,
                                                           const TopOfBook& quote) const {
    if (position < 0) {
        throw std::invalid_argument("continuation execution requires unlevered long/cash inventory");
    }
    const auto signal = strategy_.decide(history, completed, at, position > 0);
    const bool long_position =
        signal.action == ContinuationAction::enter_long || signal.action == ContinuationAction::hold_long;
    return {signal, policy_.make(identity(), strategy_.session().symbol(), signal.at, long_position, quote)};
}

TargetIntent ContinuationOrderAdapter::liquidation(const TopOfBook& quote) const {
    return policy_.make(identity(), strategy_.session().symbol(), strategy_.liquidation_time(), false, quote);
}

const IntradayContinuationStrategy& ContinuationOrderAdapter::strategy() const noexcept {
    return strategy_;
}

ReversionOrderAdapter::ReversionOrderAdapter(HistoricalBarRequest session, Shares shares, Cents fee,
                                             std::int64_t age, std::int64_t window)
    : session_{std::move(session)}, policy_{shares, fee, age, window} {
    static_cast<void>(OpeningAnchorMeasurement{}.decision_time(session_));
}

ReversionOrderDecision ReversionOrderAdapter::entry(const ReversionHistory& history,
                                                    std::span<const MinuteBar> completed,
                                                    const TopOfBook& quote) const {
    auto signal = strategy_.decide(session_, completed, history);
    auto target = policy_.make("MR1", session_.symbol(), signal.at, signal.actions[0].enter, quote);
    return {std::move(signal), std::move(target)};
}

std::chrono::sys_seconds ReversionOrderAdapter::entry_time() const {
    return OpeningAnchorMeasurement{}.decision_time(session_);
}

std::chrono::sys_seconds ReversionOrderAdapter::liquidation_time() const {
    return entry_time() + std::chrono::minutes{30};
}

TargetIntent ReversionOrderAdapter::liquidation(const TopOfBook& quote) const {
    return policy_.make("MR1", session_.symbol(), liquidation_time(), false, quote);
}
} // namespace trading::execution
