#include "order_runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace trading::execution {
namespace {

std::int64_t add(std::int64_t a, std::int64_t b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<std::int64_t>::max() - b) {
        throw std::invalid_argument("nonnegative unit sum exceeds int64 range");
    }
    return a + b;
}
std::int64_t multiply(std::int64_t a, std::int64_t b) {
    if (a < 0 || b < 0 || (b != 0 && a > std::numeric_limits<std::int64_t>::max() / b)) {
        throw std::invalid_argument("nonnegative unit product exceeds int64 range");
    }
    return a * b;
}
bool token(const std::string& value) {
    return !value.empty() && value.size() <= 128 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        });
}
bool terminal(OrderStatus status) {
    return status == OrderStatus::filled || status == OrderStatus::canceled || status == OrderStatus::rejected;
}
bool same_execution(const BrokerReport& a, const BrokerReport& b) {
    return a.order_id == b.order_id && a.fill_shares == b.fill_shares &&
           a.fill_price_cents == b.fill_price_cents && a.fee_cents == b.fee_cents;
}
void check_report(const BrokerReport& report) {
    if (!token(report.event_id) || !token(report.order_id)) {
        throw std::invalid_argument("report requires valid event and order IDs");
    }
    if (report.kind == ReportKind::fill) {
        if (!token(report.execution_id) || report.fill_shares <= 0 || report.fill_price_cents <= 0 ||
            report.fee_cents < 0 || report.cumulative_filled_shares != 0 || !report.cancel_id.empty()) {
            throw std::invalid_argument("fill requires execution ID, positive units and nonnegative fee");
        }
    } else {
        if (report.kind != ReportKind::accepted && report.kind != ReportKind::canceled &&
            report.kind != ReportKind::rejected && report.kind != ReportKind::cancel_rejected) {
            throw std::invalid_argument("unknown broker report kind");
        }
        if (!report.execution_id.empty() || report.fill_shares != 0 || report.fill_price_cents != 0 ||
            report.fee_cents != 0 || report.cumulative_filled_shares < 0 ||
            ((report.kind == ReportKind::accepted || report.kind == ReportKind::cancel_rejected) &&
             report.cumulative_filled_shares != 0)) {
            throw std::invalid_argument("non-fill report contains invalid or unused execution fields");
        }
        if ((report.kind == ReportKind::cancel_rejected && !token(report.cancel_id)) ||
            (report.kind == ReportKind::canceled && !report.cancel_id.empty() && !token(report.cancel_id)) ||
            ((report.kind == ReportKind::accepted || report.kind == ReportKind::rejected) && !report.cancel_id.empty())) {
            throw std::invalid_argument("report contains an invalid or unexpected cancel ID");
        }
    }
}

} // namespace

std::string_view name(Side side) {
    switch (side) { case Side::buy: return "buy"; case Side::sell: return "sell"; }
    throw std::invalid_argument("unknown order side");
}
std::string_view name(OrderStatus status) {
    switch (status) {
    case OrderStatus::pending_new: return "pending_new";
    case OrderStatus::working: return "working";
    case OrderStatus::partially_filled: return "partially_filled";
    case OrderStatus::pending_cancel: return "pending_cancel";
    case OrderStatus::filled: return "filled";
    case OrderStatus::canceled: return "canceled";
    case OrderStatus::rejected: return "rejected";
    }
    throw std::invalid_argument("unknown order status");
}
std::string_view name(ReportKind kind) {
    switch (kind) {
    case ReportKind::accepted: return "accepted";
    case ReportKind::fill: return "fill";
    case ReportKind::canceled: return "canceled";
    case ReportKind::rejected: return "rejected";
    case ReportKind::cancel_rejected: return "cancel_rejected";
    }
    throw std::invalid_argument("unknown report kind");
}
std::string_view name(Outcome outcome) {
    switch (outcome) {
    case Outcome::applied: return "applied";
    case Outcome::duplicate: return "duplicate";
    case Outcome::obsolete: return "obsolete";
    case Outcome::denied: return "denied";
    case Outcome::quarantined: return "quarantined";
    }
    throw std::invalid_argument("unknown runtime outcome");
}

OrderRuntime::OrderRuntime(std::string symbol, Cents cash, RuntimeLimits limits)
    : symbol_{std::move(symbol)}, limits_{limits}, state_{cash, 0, {}, {}, {}, {}, {}, ""} {
    if (!token(symbol_) || cash <= 0 || limits_.maximum_position_shares <= 0 || limits_.maximum_open_orders == 0) {
        throw std::invalid_argument("runtime requires a symbol, positive cash and positive limits");
    }
}
AccountSnapshot OrderRuntime::snapshot(const State& state) const {
    AccountSnapshot value{state.cash, state.position, 0, 0, 0, 0, connected_, reconciliation_required_};
    for (const auto& [id, order] : state.orders) {
        static_cast<void>(id);
        if (terminal(order.status)) { continue; }
        ++value.open_orders;
        const auto remaining = order.request.shares - order.filled_shares;
        if (order.request.side == Side::buy) {
            value.reserved_cash_cents = add(value.reserved_cash_cents,
                multiply(remaining, add(order.request.limit_cents, order.request.fee_per_share_ceiling_cents)));
            value.outstanding_buy_shares = add(value.outstanding_buy_shares, remaining);
        } else {
            value.reserved_sell_shares = add(value.reserved_sell_shares, remaining);
        }
    }
    if (value.cash_cents < value.reserved_cash_cents || value.position_shares < value.reserved_sell_shares ||
        add(value.position_shares, value.outstanding_buy_shares) > limits_.maximum_position_shares ||
        value.open_orders > limits_.maximum_open_orders) {
        throw std::invalid_argument("cash, inventory, position or open-order limit exceeded");
    }
    return value;
}
AccountSnapshot OrderRuntime::account() const { return snapshot(state_); }
std::string_view OrderRuntime::symbol() const noexcept { return symbol_; }
OrderView OrderRuntime::order(const std::string& id) const { return state_.orders.at(id); }
std::vector<OrderView> OrderRuntime::orders() const {
    std::vector<OrderView> result;
    for (const auto& [id, item] : state_.orders) { static_cast<void>(id); result.push_back(item); }
    return result;
}
std::optional<TargetIntent> OrderRuntime::latest_target() const {
    if (state_.latest_target_id.empty()) { return std::nullopt; }
    return state_.targets.at(state_.latest_target_id);
}
const std::vector<TraceEntry>& OrderRuntime::trace() const noexcept { return trace_; }
Outcome OrderRuntime::record(std::string input, std::string id, std::string event, Outcome outcome, std::string reason) {
    std::string status;
    if (const auto item = state_.orders.find(id); item != state_.orders.end()) { status = name(item->second.status); }
    trace_.push_back({trace_.size() + 1, std::move(input), std::move(id), std::move(event),
                      outcome, std::move(reason), std::move(status), account()});
    return outcome;
}
Outcome OrderRuntime::submit(const OrderRequest& request) {
    if (!connected_ || reconciliation_required_) {
        return record("submit", request.id, "", Outcome::denied, "entry gate requires connection and reconciliation");
    }
    try {
        static_cast<void>(name(request.side));
        if (!token(request.id) || request.symbol != symbol_ || request.shares <= 0 || request.limit_cents <= 0 ||
            request.fee_per_share_ceiling_cents < 0 || request.fee_per_share_ceiling_cents >= request.limit_cents ||
            state_.orders.contains(request.id)) {
            throw std::invalid_argument("invalid order terms or reused order ID");
        }
        State candidate = state_;
        candidate.orders.emplace(request.id, OrderView{request, 0, OrderStatus::pending_new, false, ""});
        static_cast<void>(snapshot(candidate));
        state_ = std::move(candidate);
    } catch (const std::invalid_argument& error) {
        return record("submit", request.id, "", Outcome::denied, error.what());
    }
    return record("submit", request.id, "", Outcome::applied, "resources reserved before dispatch");
}
Outcome OrderRuntime::register_target(const TargetIntent& target) {
    if (const auto previous = state_.targets.find(target.id); previous != state_.targets.end()) {
        if (previous->second == target) {
            return record("target", "", target.id, Outcome::duplicate, "identical strategy target already recorded");
        }
        reconciliation_required_ = true;
        return record("target", "", target.id, Outcome::quarantined, "conflicting strategy target ID");
    }
    if (!connected_ || reconciliation_required_) {
        return record("target", "", target.id, Outcome::denied, "target requires connection and reconciliation");
    }
    if (!token(target.id) || target.id.size() > 40 || target.symbol != symbol_ || target.decision_at_seconds <= 0 ||
        target.expires_at_seconds < target.decision_at_seconds || target.target_shares < 0 ||
        target.target_shares > limits_.maximum_position_shares || target.sell_limit_cents <= 0 ||
        target.buy_limit_cents < target.sell_limit_cents || target.fee_per_share_ceiling_cents < 0 ||
        target.fee_per_share_ceiling_cents >= target.sell_limit_cents ||
        (!state_.latest_target_id.empty() && target.decision_at_seconds <= latest_target()->decision_at_seconds)) {
        return record("target", "", target.id, Outcome::denied, "invalid, noncausal or out-of-limit strategy target");
    }
    auto candidate = state_;
    candidate.targets.emplace(target.id, target);
    candidate.latest_target_id = target.id;
    state_ = std::move(candidate);
    return record("target", "", target.id, Outcome::applied, "strategy target recorded before order routing");
}
Outcome OrderRuntime::request_cancel(const std::string& id, const std::string& cancel_id) {
    const auto item = state_.orders.find(id);
    if (const auto previous = state_.cancel_requests.find(cancel_id); previous != state_.cancel_requests.end()) {
        return record("cancel", id, cancel_id, previous->second == id ? Outcome::duplicate : Outcome::denied,
                      "cancel ID already used");
    }
    if (!connected_ || !token(cancel_id) || item == state_.orders.end() || terminal(item->second.status)) {
        return record("cancel", id, cancel_id, Outcome::denied, "cancel requires connection, valid ID and an open order");
    }
    if (item->second.status == OrderStatus::pending_cancel) {
        return record("cancel", id, cancel_id, Outcome::denied, "another cancel already pending");
    }
    state_.cancel_requests.emplace(cancel_id, id);
    item->second.status = OrderStatus::pending_cancel;
    item->second.pending_cancel_id = cancel_id;
    return record("cancel", id, cancel_id, Outcome::applied, "reservation retained until terminal confirmation");
}
Outcome OrderRuntime::apply(State& candidate, const BrokerReport& report) const {
    const auto found = candidate.orders.find(report.order_id);
    if (found == candidate.orders.end()) { throw std::invalid_argument("report references an unknown order"); }
    auto& order = found->second;
    const auto& request = order.request;
    if (!report.cancel_id.empty()) {
        const auto cancel = candidate.cancel_requests.find(report.cancel_id);
        if (cancel == candidate.cancel_requests.end() || cancel->second != report.order_id) {
            throw std::invalid_argument("report references an unknown cancel request");
        }
    }
    if (report.kind == ReportKind::fill) {
        if (const auto execution = candidate.executions.find(report.execution_id); execution != candidate.executions.end()) {
            if (!same_execution(execution->second, report)) { throw std::invalid_argument("conflicting execution ID"); }
            return Outcome::duplicate;
        }
        if (terminal(order.status) || report.fill_shares > request.shares - order.filled_shares ||
            (request.side == Side::buy && report.fill_price_cents > request.limit_cents) ||
            (request.side == Side::sell && report.fill_price_cents < request.limit_cents) ||
            report.fee_cents > multiply(report.fill_shares, request.fee_per_share_ceiling_cents)) {
            throw std::invalid_argument("fill contradicts order quantity, limit, fee ceiling or terminal state");
        }
        const auto notional = multiply(report.fill_shares, report.fill_price_cents);
        if (request.side == Side::buy) {
            const auto debit = add(notional, report.fee_cents);
            if (debit > candidate.cash) { throw std::invalid_argument("fill exceeds cash"); }
            candidate.cash -= debit;
            candidate.position = add(candidate.position, report.fill_shares);
        } else {
            if (report.fill_shares > candidate.position) { throw std::invalid_argument("fill exceeds inventory"); }
            candidate.cash = add(candidate.cash, notional - report.fee_cents);
            candidate.position -= report.fill_shares;
        }
        order.filled_shares += report.fill_shares;
        order.acknowledged = true;
        if (order.filled_shares == request.shares) {
            order.status = OrderStatus::filled;
            order.pending_cancel_id.clear();
        }
        else if (order.status != OrderStatus::pending_cancel) { order.status = OrderStatus::partially_filled; }
        candidate.executions.emplace(report.execution_id, report);
        return Outcome::applied;
    }
    if (report.kind == ReportKind::accepted) {
        if (order.status == OrderStatus::rejected) {
            throw std::invalid_argument("new-order acknowledgement contradicts terminal rejection");
        }
        order.acknowledged = true;
        if (order.status != OrderStatus::pending_new) { return Outcome::obsolete; }
        order.status = OrderStatus::working;
        return Outcome::applied;
    }
    if (report.kind == ReportKind::cancel_rejected) {
        if (terminal(order.status) || report.cancel_id != order.pending_cancel_id) { return Outcome::obsolete; }
        order.status = order.filled_shares == 0 ? OrderStatus::working : OrderStatus::partially_filled;
        order.acknowledged = true;
        order.pending_cancel_id.clear();
        return Outcome::applied;
    }
    if (report.cumulative_filled_shares != order.filled_shares) {
        throw std::invalid_argument("terminal cumulative quantity differs from received executions");
    }
    if (report.kind == ReportKind::rejected) {
        if (order.status == OrderStatus::rejected) { return Outcome::obsolete; }
        if (order.acknowledged || order.filled_shares != 0 ||
            (order.status != OrderStatus::pending_new && order.status != OrderStatus::pending_cancel)) {
            throw std::invalid_argument("new-order rejection contradicts prior acknowledgement or executions");
        }
        order.status = OrderStatus::rejected;
    } else {
        if (order.status == OrderStatus::canceled || order.status == OrderStatus::filled) { return Outcome::obsolete; }
        if (order.status == OrderStatus::rejected) { throw std::invalid_argument("cancel confirmation contradicts new-order rejection"); }
        // Broker-initiated cancellation is also valid. Cumulative quantity must
        // match the ledger before any remaining reservation can be released.
        order.status = OrderStatus::canceled;
    }
    order.pending_cancel_id.clear();
    return Outcome::applied;
}
Outcome OrderRuntime::receive(const BrokerReport& report) {
    Outcome outcome = Outcome::quarantined;
    try {
        check_report(report);
        if (const auto event = state_.events.find(report.event_id); event != state_.events.end()) {
            if (event->second != report) { throw std::invalid_argument("conflicting event ID"); }
            return record("report", report.order_id, report.event_id, Outcome::duplicate, "identical event already processed");
        }
        State candidate = state_;
        outcome = apply(candidate, report);
        candidate.events.emplace(report.event_id, report);
        static_cast<void>(snapshot(candidate));
        state_ = std::move(candidate);
    } catch (const std::invalid_argument& error) {
        reconciliation_required_ = true;
        return record("report", report.order_id, report.event_id, Outcome::quarantined, error.what());
    }
    return record("report", report.order_id, report.event_id, outcome,
                  outcome == Outcome::duplicate ? "execution already booked" :
                  outcome == Outcome::obsolete ? "acknowledgement cannot regress order state" : "broker report applied");
}
void OrderRuntime::disconnect() {
    connected_ = false;
    reconciliation_required_ = true;
    static_cast<void>(record("disconnect", "", "", Outcome::applied, "entry gate closed; reservations retained"));
}
void OrderRuntime::reconnect() {
    connected_ = true;
    reconciliation_required_ = true;
    static_cast<void>(record("reconnect", "", "", Outcome::applied, "connection restored; reconciliation still required"));
}
Outcome OrderRuntime::reconcile(const BrokerSnapshot& broker) {
    bool matches = connected_ && broker.cash_cents == state_.cash && broker.position_shares == state_.position;
    std::map<std::string, BrokerOpenOrder> open;
    for (const auto& item : broker.open_orders) {
        if (!open.emplace(item.request.id, item).second) { matches = false; }
    }
    std::size_t expected = 0;
    for (const auto& [id, item] : state_.orders) {
        if (terminal(item.status)) { continue; }
        ++expected;
        const auto found = open.find(id);
        if (found == open.end() || found->second != BrokerOpenOrder{item.request, item.filled_shares}) { matches = false; }
    }
    matches = matches && expected == open.size();
    reconciliation_required_ = !matches;
    return record("reconcile", "", "", matches ? Outcome::applied : Outcome::quarantined,
                  matches ? "cash, position and complete open-order terms match" : "broker snapshot differs or connection is absent");
}

} // namespace trading::execution
