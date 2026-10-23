#include "simulated_broker.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace trading::execution {
namespace {
Cents product(Shares q, Cents p) {
    if (q < 0 || p < 0 || (p != 0 && q > std::numeric_limits<Cents>::max() / p)) {
        throw std::invalid_argument("simulated execution product exceeds integer units");
    }
    return q * p;
}
Cents sum(Cents a, Cents b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Cents>::max() - b) {
        throw std::invalid_argument("simulated account sum exceeds integer units");
    }
    return a + b;
}
} // namespace

SimulatedBroker::SimulatedBroker(std::string symbol, Cents cash, Cents fee)
    : symbol_{std::move(symbol)}, initial_cash_{cash}, fee_per_share_{fee} {
    if (symbol_.empty() || cash <= 0 || fee < 0) { throw std::invalid_argument("invalid simulated broker configuration"); }
}
void SimulatedBroker::advance_time(std::int64_t now) {
    if (now <= 0 || now < now_) { throw std::invalid_argument("simulated broker clock cannot regress"); }
    now_ = now;
}
void SimulatedBroker::report(const std::string& id, ReportKind kind, std::string execution, Shares quantity,
                             Cents price, Cents fee, Shares cumulative, std::string cancel_id) {
    reports_.push_back({"SIM-E" + std::to_string(reports_.size() + 1), id, kind, std::move(execution),
                        quantity, price, fee, cumulative, std::move(cancel_id)});
}
bool SimulatedBroker::on_quote(const TopOfBook& value) {
    if (const auto old = quotes_.find(value.sequence); old != quotes_.end()) {
        if (old->second != value) { throw std::invalid_argument("conflicting simulated quote sequence"); }
        return false;
    }
    if (value.sequence == 0 || value.bid_cents <= 0 || value.ask_cents < value.bid_cents ||
        value.bid_shares < 0 || value.ask_shares < 0 || (quote_ && value.sequence <= quote_->sequence)) {
        throw std::invalid_argument("invalid or out-of-order simulated quote");
    }
    auto next = *this;
    next.advance_time(value.at_seconds);
    next.quote_ = value;
    next.quotes_.emplace(value.sequence, value);
    next.bid_remaining_ = value.bid_shares;
    next.ask_remaining_ = value.ask_shares;
    next.match();
    *this = std::move(next);
    return true;
}
bool SimulatedBroker::submit(const OrderRequest& request, std::int64_t now) {
    if (const auto previous = orders_.find(request.id); previous != orders_.end()) {
        if (previous->second.request != request) { throw std::invalid_argument("conflicting simulated order ID"); }
        return false;
    }
    static_cast<void>(name(request.side));
    if (request.id.empty() || request.symbol != symbol_ || request.shares <= 0 || request.limit_cents <= 0 ||
        request.fee_per_share_ceiling_cents < 0) { throw std::invalid_argument("invalid simulated order"); }
    static_cast<void>(product(request.shares, request.limit_cents));
    auto next = *this;
    next.advance_time(now);
    const bool accepted = request.fee_per_share_ceiling_cents >= fee_per_share_ && request.limit_cents > fee_per_share_;
    next.orders_.emplace(request.id, Order{request, 0, accepted});
    next.fifo_.push_back(request.id);
    next.report(request.id, accepted ? ReportKind::accepted : ReportKind::rejected);
    if (accepted) { next.match(); }
    *this = std::move(next);
    return true;
}
bool SimulatedBroker::cancel(const std::string& id, const std::string& cancel_id, std::int64_t now) {
    if (const auto old = cancel_ids_.find(cancel_id); old != cancel_ids_.end()) {
        if (old->second != id) { throw std::invalid_argument("conflicting simulated cancellation ID"); }
        return false;
    }
    if (!orders_.contains(id) || cancel_id.empty()) { throw std::invalid_argument("unknown simulated cancellation"); }
    auto next = *this;
    next.advance_time(now);
    next.cancel_ids_.emplace(cancel_id, id);
    next.pending_cancels_.emplace_back(cancel_id, id);
    *this = std::move(next);
    return true;
}
void SimulatedBroker::complete_cancels(std::int64_t now) {
    auto next = *this;
    next.advance_time(now);
    for (const auto& [cancel_id, id] : next.pending_cancels_) {
        auto& order = next.orders_.at(id);
        if (order.active) {
            order.active = false;
            next.report(id, ReportKind::canceled, "", 0, 0, 0, order.filled, cancel_id);
        } else { next.report(id, ReportKind::cancel_rejected, "", 0, 0, 0, 0, cancel_id); }
    }
    next.pending_cancels_.clear();
    *this = std::move(next);
}
void SimulatedBroker::match() {
    if (!quote_) { return; }
    for (const auto& id : fifo_) {
        auto& order = orders_.at(id);
        if (!order.active) { continue; }
        const bool buy = order.request.side == Side::buy;
        const auto price = buy ? quote_->ask_cents : quote_->bid_cents;
        if ((buy && price > order.request.limit_cents) || (!buy && price < order.request.limit_cents)) { continue; }
        auto& remaining = buy ? ask_remaining_ : bid_remaining_;
        const auto quantity = std::min(remaining, order.request.shares - order.filled);
        if (quantity == 0) { continue; }
        const auto fee = product(quantity, fee_per_share_);
        const auto execution = "SIM-X" + std::to_string(fills_.size() + 1);
        fills_.push_back({execution, id, order.request.side, quantity, price, fee, quote_->sequence});
        static_cast<void>(snapshot()); // Broker truth is independently summed from executions.
        order.filled += quantity;
        remaining -= quantity;
        if (order.filled == order.request.shares) { order.active = false; }
        report(id, ReportKind::fill, execution, quantity, price, fee);
    }
}
BrokerSnapshot SimulatedBroker::snapshot() const {
    BrokerSnapshot result{initial_cash_, 0, {}};
    for (const auto& fill : fills_) {
        const auto gross = product(fill.shares, fill.price_cents);
        if (fill.side == Side::buy) {
            const auto debit = sum(gross, fill.fee_cents);
            if (debit > result.cash_cents) { throw std::invalid_argument("simulated broker has insufficient cash"); }
            result.cash_cents -= debit;
            result.position_shares = sum(result.position_shares, fill.shares);
        } else {
            if (fill.shares > result.position_shares || fill.fee_cents > gross) { throw std::invalid_argument("simulated broker cannot sell unavailable shares"); }
            result.position_shares -= fill.shares;
            result.cash_cents = sum(result.cash_cents, gross - fill.fee_cents);
        }
    }
    for (const auto& [id, order] : orders_) {
        static_cast<void>(id);
        if (order.active) { result.open_orders.push_back({order.request, order.filled}); }
    }
    return result;
}
std::optional<TopOfBook> SimulatedBroker::quote() const { return quote_; }
const std::vector<BrokerReport>& SimulatedBroker::reports() const noexcept { return reports_; }
const std::vector<SimulatedFill>& SimulatedBroker::fills() const noexcept { return fills_; }
std::size_t SimulatedBroker::order_count() const noexcept { return orders_.size(); }
std::int64_t SimulatedBroker::now_seconds() const noexcept { return now_; }
std::string_view SimulatedBroker::symbol() const noexcept { return symbol_; }

} // namespace trading::execution
