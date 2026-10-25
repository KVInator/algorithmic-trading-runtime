#pragma once

#include "intraday_continuation.hpp"
#include "reversion_strategy.hpp"
#include "simulated_broker.hpp"

namespace trading::execution {

// Separate execution sizing convention: one fixed whole-share long target.
// This does not change the fractional research accounts or their frozen rules.
class WholeShareTargetPolicy {
public:
    WholeShareTargetPolicy(Shares long_shares, Cents fee_ceiling, std::int64_t maximum_quote_age = 2,
                           std::int64_t dispatch_window = 30);
    [[nodiscard]] TargetIntent make(std::string_view identity, std::string_view symbol,
                                    std::chrono::sys_seconds at, bool long_position,
                                    const TopOfBook& quote) const;

private:
    Shares long_shares_;
    Cents fee_ceiling_;
    std::int64_t quote_age_;
    std::int64_t dispatch_window_;
};

struct ContinuationOrderDecision {
    IntradayContinuationDecision signal;
    TargetIntent target;
};

class ContinuationOrderAdapter {
public:
    ContinuationOrderAdapter(IntradayContinuationStrategy strategy, Shares long_shares, Cents fee_ceiling,
                             std::int64_t quote_age = 2, std::int64_t dispatch_window = 30);
    [[nodiscard]] ContinuationOrderDecision decide(const IntradayContinuationHistory& history,
                                                   std::span<const MinuteBar> completed,
                                                   std::chrono::sys_seconds at, Shares executed_position,
                                                   const TopOfBook& quote) const;
    [[nodiscard]] TargetIntent liquidation(const TopOfBook& quote) const;
    [[nodiscard]] const IntradayContinuationStrategy& strategy() const noexcept;

private:
    [[nodiscard]] std::string_view identity() const noexcept;
    IntradayContinuationStrategy strategy_;
    WholeShareTargetPolicy policy_;
};

struct ReversionOrderDecision {
    ReversionDecision signal;
    TargetIntent target;
};

class ReversionOrderAdapter {
public:
    ReversionOrderAdapter(HistoricalBarRequest session, Shares long_shares, Cents fee_ceiling,
                          std::int64_t quote_age = 2, std::int64_t dispatch_window = 30);
    [[nodiscard]] ReversionOrderDecision entry(const ReversionHistory& history,
                                               std::span<const MinuteBar> completed,
                                               const TopOfBook& quote) const;
    [[nodiscard]] TargetIntent liquidation(const TopOfBook& quote) const;
    [[nodiscard]] std::chrono::sys_seconds entry_time() const;
    [[nodiscard]] std::chrono::sys_seconds liquidation_time() const;

private:
    HistoricalBarRequest session_;
    WholeShareTargetPolicy policy_;
    ReversionStrategy strategy_{};
};
} // namespace trading::execution
