#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace trading::execution {

// This broker-message model uses whole shares and integer USD cents. It is
// distinct from the fractional target-weight account used in historical studies.
using Shares = std::int64_t;
using Cents = std::int64_t;
enum class Side { buy, sell };
enum class OrderStatus { pending_new, working, partially_filled, pending_cancel, filled, canceled, rejected };
enum class ReportKind { accepted, fill, canceled, rejected, cancel_rejected };
enum class Outcome { applied, duplicate, obsolete, denied, quarantined };

struct OrderRequest {
    std::string id;
    std::string symbol;
    Side side;
    Shares shares;
    Cents limit_cents;
    Cents fee_per_share_ceiling_cents;
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
    Cents fill_price_cents{0};
    Cents fee_cents{0};
    Shares cumulative_filled_shares{0};
    std::string cancel_id;
    bool operator==(const BrokerReport&) const = default;
};
struct RuntimeLimits {
    Shares maximum_position_shares;
    std::size_t maximum_open_orders;
};
struct AccountSnapshot {
    Cents cash_cents;
    Shares position_shares;
    Cents reserved_cash_cents;
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
    Cents cash_cents;
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
    OrderRuntime(std::string symbol, Cents initial_cash_cents, RuntimeLimits limits);
    [[nodiscard]] Outcome submit(const OrderRequest& request);
    [[nodiscard]] Outcome request_cancel(const std::string& order_id, const std::string& cancel_id);
    [[nodiscard]] Outcome receive(const BrokerReport& report);
    void disconnect();
    void reconnect();
    // Exact comparison, including every open order and its immutable terms.
    // Matching snapshots clear the gate; mismatches never overwrite local state.
    [[nodiscard]] Outcome reconcile(const BrokerSnapshot& broker);
    [[nodiscard]] AccountSnapshot account() const;
    [[nodiscard]] OrderView order(const std::string& id) const;
    [[nodiscard]] const std::vector<TraceEntry>& trace() const noexcept;
private:
    struct State {
        Cents cash;
        Shares position{0};
        std::map<std::string, OrderView> orders;
        std::map<std::string, BrokerReport> events;
        std::map<std::string, BrokerReport> executions;
        std::map<std::string, std::string> cancel_requests;
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
