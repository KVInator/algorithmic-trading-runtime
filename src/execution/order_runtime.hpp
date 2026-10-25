#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trading::execution {

// Integer arithmetic/state engine. The owning journal contract selects the USD
// unit: cents in legacy schemas 1/2; nanodollars in the paper bridge schema 3.
// Quantities remain whole shares. Serialized legacy cent keys are preserved.
using Shares = std::int64_t;
using MoneyUnits = std::int64_t;
using Cents = MoneyUnits; // Legacy caller convention; new owners declare their scale.
enum class Side { buy, sell };
enum class OrderStatus { pending_new, working, partially_filled, pending_cancel, filled, canceled, rejected };
enum class ReportKind { accepted, fill, canceled, rejected, cancel_rejected };
enum class Outcome { applied, duplicate, obsolete, denied, quarantined };

struct OrderRequest {
    std::string id;
    std::string symbol;
    Side side;
    Shares shares;
    MoneyUnits limit_units;
    MoneyUnits fee_per_share_ceiling_units;
    bool operator==(const OrderRequest&) const = default;
};
struct OrderView {
    OrderRequest request;
    Shares filled_shares{0};
    OrderStatus status{OrderStatus::pending_new};
    bool acknowledged{false};
    std::string pending_cancel_id;
    bool operator==(const OrderView&) const = default;
};
struct BrokerReport {
    std::string event_id;
    std::string order_id;
    ReportKind kind;
    // Fill fields are incremental, keyed by a stable execution ID. Only terminal
    // cancel/reject reports use cumulative_filled_shares; unused fields are zero.
    std::string execution_id;
    Shares fill_shares{0};
    MoneyUnits fill_price_units{0};
    MoneyUnits fee_units{0};
    Shares cumulative_filled_shares{0};
    std::string cancel_id;
    bool operator==(const BrokerReport&) const = default;
};
struct RuntimeLimits {
    Shares maximum_position_shares;
    std::size_t maximum_open_orders;
};
struct TargetIntent {
    std::string id;
    std::string symbol;
    std::int64_t decision_at_seconds;
    std::int64_t expires_at_seconds;
    Shares target_shares;
    MoneyUnits buy_limit_units;
    MoneyUnits sell_limit_units;
    MoneyUnits fee_per_share_ceiling_units;
    bool operator==(const TargetIntent&) const = default;
};
struct AccountSnapshot {
    MoneyUnits cash_units;
    Shares position_shares;
    MoneyUnits reserved_cash_units;
    Shares reserved_sell_shares;
    Shares outstanding_buy_shares;
    std::size_t open_orders;
    bool connected;
    bool reconciliation_required;
    bool operator==(const AccountSnapshot&) const = default;
};
struct BrokerOpenOrder {
    OrderRequest request;
    Shares filled_shares;
    bool operator==(const BrokerOpenOrder&) const = default;
};
struct BrokerSnapshot {
    MoneyUnits cash_units;
    Shares position_shares;
    std::vector<BrokerOpenOrder> open_orders;
};
struct TraceEntry {
    std::size_t sequence;
    std::string input;
    std::string order_id;
    std::string event_id;
    Outcome outcome;
    std::string reason;
    std::string order_status;
    AccountSnapshot account;
    bool operator==(const TraceEntry&) const = default;
};

// Deterministic, single-symbol, unlevered limit-order core. Calls are serialized
// in delivery order. Invalid broker messages retain accounting and reservations,
// latch the entry gate and remain visible in the trace. No transport or clock.
class OrderRuntime {
public:
    OrderRuntime(std::string symbol, MoneyUnits initial_cash_units, RuntimeLimits limits);
    [[nodiscard]] Outcome submit(const OrderRequest& request);
    [[nodiscard]] Outcome register_target(const TargetIntent& target);
    [[nodiscard]] Outcome request_cancel(const std::string& order_id, const std::string& cancel_id);
    [[nodiscard]] Outcome receive(const BrokerReport& report);
    void disconnect();
    void reconnect();
    // Exact comparison, including every open order and its immutable terms.
    // Matching snapshots clear the gate; mismatches never overwrite local state.
    [[nodiscard]] Outcome reconcile(const BrokerSnapshot& broker);
    [[nodiscard]] AccountSnapshot account() const;
    [[nodiscard]] std::string_view symbol() const noexcept;
    [[nodiscard]] OrderView order(const std::string& id) const;
    [[nodiscard]] std::vector<OrderView> orders() const;
    [[nodiscard]] std::optional<TargetIntent> latest_target() const;
    [[nodiscard]] const std::vector<TraceEntry>& trace() const noexcept;
private:
    struct State {
        MoneyUnits cash;
        Shares position{0};
        std::map<std::string, OrderView> orders;
        std::map<std::string, BrokerReport> events;
        std::map<std::string, BrokerReport> executions;
        std::map<std::string, std::string> cancel_requests;
        std::map<std::string, TargetIntent> targets;
        std::string latest_target_id;
    };
    [[nodiscard]] AccountSnapshot snapshot(const State& state) const;
    [[nodiscard]] Outcome record(std::string input, std::string order_id,
                                 std::string event_id, Outcome outcome, std::string reason);
    [[nodiscard]] Outcome apply(State& candidate, const BrokerReport& report) const;
    std::string symbol_;
    RuntimeLimits limits_;
    State state_;
    bool connected_{true};
    bool reconciliation_required_{false};
    std::vector<TraceEntry> trace_;
};

[[nodiscard]] std::string_view name(Side side);
[[nodiscard]] std::string_view name(OrderStatus status);
[[nodiscard]] std::string_view name(ReportKind kind);
[[nodiscard]] std::string_view name(Outcome outcome);

} // namespace trading::execution
