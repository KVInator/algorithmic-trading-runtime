#include "paper_execution.hpp"
#include "runtime_json.hpp"
#include "content_digest.hpp"
#include <algorithm>
#include <set>

namespace trading::execution {
namespace {
using Json = nlohmann::json;

MoneyUnits nanos(std::string_view value) {
    return BrokerDecimal{value}.scaled_integer(9);
}

MoneyUnits from_cents(MoneyUnits value) {
    return BrokerDecimal{std::to_string(value)}.scaled_integer(7);
}

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::invalid_argument(std::string{message});
}

bool terminal(OrderStatus s) {
    return s == OrderStatus::filled || s == OrderStatus::canceled || s == OrderStatus::rejected;
}

Json order_units(const OrderView& o) {
    return {{"client_id", o.request.id},
            {"symbol", o.request.symbol},
            {"side", name(o.request.side)},
            {"shares", o.request.shares},
            {"limit_units", o.request.limit_units},
            {"fee_ceiling_units", o.request.fee_per_share_ceiling_units},
            {"filled_shares", o.filled_shares},
            {"status", name(o.status)},
            {"acknowledged", o.acknowledged},
            {"pending_cancel_id", o.pending_cancel_id}};
}

void shape(const Json& command, std::initializer_list<const char*> keys) {
    require(command.is_object() && command.size() == keys.size(), "unexpected paper command shape");
    for (const auto key : keys)
        require(command.contains(key), "missing paper command field");
}
} // namespace

PaperExecutionState::PaperExecutionState(const PaperExecutionConfiguration& c)
    : configuration_{c}, core_{"SPY", nanos(c.initial_cash_usd), c.limits} {
    require(c.fee_policy == "paper_zero_fees_v1", "unsupported paper fee policy");
    // Use the provider decoder to validate account identity without introducing a second UUID parser.
    static_cast<void>(AlpacaPaperDecoder{}.account(Json{
        {"id", c.account_id},
        {"cash", c.initial_cash_usd},
        {"currency", "USD"},
        {"status", "ACTIVE"},
        {"account_blocked", false},
        {"trading_blocked", false}}.dump()));
    require(c.limits.maximum_open_orders <= 100, "paper bridge supports at most 100 tracked open orders");
}

AccountSnapshot PaperExecutionState::account() const {
    return core_.account();
}

Json PaperExecutionState::snapshot() const {
    Json orders = Json::array();
    for (const auto& o : core_.orders())
        orders.push_back(order_units(o));
    return {{"money_unit", "USD_1e-9"},
            {"fee_policy", configuration_.fee_policy},
            {"account", account_units_json(core_.account())},
            {"authorized", authorized_},
            {"subscribed", subscribed_},
            {"unresolved_evidence", unresolved_},
            {"orders", orders},
            {"broker_bindings", bindings_},
            {"execution_fingerprints", executions_},
            {"event_fingerprints", events_}};
}

PaperOutcome PaperExecutionState::apply(const Json& command) {
    auto candidate = *this;
    try {
        auto result = candidate.handle(command);
        *this = std::move(candidate);
        return result;
    } catch (const std::invalid_argument& e) {
        core_.disconnect();
        unresolved_ = true;
        return {Outcome::quarantined, e.what()};
    } catch (const Json::exception&) {
        core_.disconnect();
        unresolved_ = true;
        return {Outcome::quarantined, "invalid paper command data types"};
    } catch (const std::out_of_range&) {
        core_.disconnect();
        unresolved_ = true;
        return {Outcome::quarantined, "broker message references an unknown client order"};
    }
}

OrderView PaperExecutionState::bind(const PaperOrder& order) {
    static const std::set<std::string> supported{"accepted",         "pending_new",    "new",
                                                 "partially_filled", "pending_cancel", "filled",
                                                 "canceled",         "expired",        "rejected"};
    require(supported.contains(order.status), "unsupported broker order status requires review");
    const auto local = core_.order(order.client_id);
    require(order.symbol == local.request.symbol && order.side == local.request.side &&
                order.quantity.scaled_integer(0) == local.request.shares &&
                nanos(order.limit_price.text()) == local.request.limit_units,
            "broker order differs from durable intent");
    if (auto found = bindings_.find(order.client_id); found != bindings_.end())
        require(found->second == order.broker_id, "client order has conflicting broker UUID");
    for (const auto& [client, broker] : bindings_)
        require(client == order.client_id || broker != order.broker_id,
                "broker UUID already belongs to another intent");
    bindings_[order.client_id] = order.broker_id;
    return local;
}

PaperOutcome PaperExecutionState::ingress(const PaperStreamMessage& message) {
    if (message.kind == PaperStreamKind::authorization) {
        authorized_ = message.authorized;
        subscribed_ = false;
        core_.disconnect();
        return {Outcome::applied, "authorization recorded; subscription/reconciliation required"};
    }
    if (message.kind == PaperStreamKind::listening) {
        subscribed_ = authorized_ && message.trade_updates_subscribed;
        if (subscribed_)
            core_.reconnect();
        else
            core_.disconnect();
        return {Outcome::applied, "subscription recorded; exact reconciliation required"};
    }
    require(authorized_ && subscribed_, "trade updates require an authorized subscription");
    const auto& u = *message.update;
    require(u.recognized_event, "unsupported broker event requires evidence review");
    const auto local = bind(u.order);
    const auto raw = Json::parse(message.raw_payload).at("data").dump();
    const auto id = u.event_id.value_or(sha256(raw));
    if (auto event = events_.find(id); event != events_.end()) {
        require(event->second == sha256(raw), "conflicting broker event identity");
        return {Outcome::duplicate, "identical broker event retained"};
    }
    BrokerReport report{id, u.order.client_id, ReportKind::accepted, "", 0, 0, 0, 0, ""};
    if (u.fill) {
        const auto& f = *u.fill;
        const auto shares = f.quantity.scaled_integer(0), price = nanos(f.price.text());
        const auto fingerprint =
            sha256(Json{{"broker_id", u.order.broker_id},
                        {"client_id", u.order.client_id},
                        {"shares", shares},
                        {"price_units", price},
                        {"cumulative", u.order.filled_quantity.text()},
                        {"position", f.position_quantity.text()},
                        {"timestamp_ns", u.timestamp->value().time_since_epoch().count()}}
                       .dump());
        if (auto old = executions_.find(f.execution_id); old != executions_.end()) {
            require(old->second == fingerprint, "conflicting execution redelivery");
            events_[id] = sha256(raw);
            return {Outcome::duplicate, "execution already booked"};
        }
        report.kind = ReportKind::fill;
        report.execution_id = f.execution_id;
        report.fill_shares = shares;
        report.fill_price_units = price;
        const auto outcome = core_.receive(report);
        require(outcome == Outcome::applied, "execution contradicts reserved order/account");
        require(core_.order(u.order.client_id).filled_shares == u.order.filled_quantity.scaled_integer(0) &&
                    core_.account().position_shares == f.position_quantity.scaled_integer(0),
                "cumulative execution or position differs from received fills");
        executions_[f.execution_id] = fingerprint;
    } else if (u.event == "accepted" || u.event == "pending_new" || u.event == "new") {
        require(u.order.filled_quantity.scaled_integer(0) <= local.filled_shares,
                "acknowledgement reveals missing executions");
        // Broker receipt/pending venue acceptance is not the venue's new-order acknowledgement.
        if (u.event == "new") {
            const auto result = core_.receive(report);
            require(result != Outcome::quarantined, "acknowledgement contradiction");
        }
    } else if (u.event == "pending_cancel") {
        require(u.order.filled_quantity.scaled_integer(0) <= local.filled_shares,
                "pending cancel reveals missing fills");
    } else {
        require(u.timestamp.has_value(), "terminal broker event requires timestamp");
        if (u.event == "canceled" || u.event == "expired" || u.event == "rejected") {
            require(u.order.status == u.event, "terminal event contradicts broker order status");
        }
        report.kind = u.event == "rejected"                ? ReportKind::rejected
                      : u.event == "order_cancel_rejected" ? ReportKind::cancel_rejected
                                                           : ReportKind::canceled;
        if (report.kind == ReportKind::cancel_rejected) {
            if (terminal(local.status)) {
                events_[id] = sha256(raw);
                return {Outcome::obsolete, "late cancellation rejection cannot regress terminal state"};
            }
            require(!local.pending_cancel_id.empty(), "unmatched cancellation rejection");
            report.cancel_id = local.pending_cancel_id;
        } else
            report.cumulative_filled_shares = u.order.filled_quantity.scaled_integer(0);
        const auto result = core_.receive(report);
        require(result != Outcome::quarantined, "terminal broker event contradicts received evidence");
    }
    events_[id] = sha256(raw);
    return {Outcome::applied, "broker event recorded under explicit zero-fee paper convention"};
}

PaperOutcome PaperExecutionState::handle(const Json& c) {
    const auto kind = c.at("kind").get<std::string>();
    if (kind == "disconnect") {
        shape(c, {"kind"});
        authorized_ = false;
        subscribed_ = false;
        core_.disconnect();
        return {Outcome::applied, "routing closed; reservations and identities retained"};
    }
    if (kind == "submit") {
        shape(c, {"kind", "order"});
        const auto input = parse_runtime_input(c);
        auto order = std::get<SubmitInput>(input).request;
        static_cast<void>(AlpacaPaperRequests{}.submit(order));
        if (unresolved_)
            return {Outcome::denied, "unresolved broker evidence requires review"};
        order.limit_units = from_cents(order.limit_units);
        order.fee_per_share_ceiling_units = from_cents(order.fee_per_share_ceiling_units);
        const auto outcome = core_.submit(order);
        return {outcome, "whole-share DAY intent evaluated before external dispatch"};
    }
    if (kind == "cancel") {
        shape(c, {"kind", "client_id", "cancel_id"});
        const auto client = c.at("client_id").get<std::string>();
        if (!bindings_.contains(client))
            return {Outcome::denied, "cancel requires resolved broker UUID"};
        const auto outcome = core_.request_cancel(client, c.at("cancel_id").get<std::string>());
        return {outcome, "cancel intent evaluated; reservation retained"};
    }
    if (kind == "stream") {
        shape(c, {"kind", "raw", "received_at"});
        return ingress(AlpacaPaperDecoder{}.stream(c.at("raw").get<std::string>(),
                                                   BrokerTimestamp{c.at("received_at").get<std::string>()}));
    }
    if (kind == "order") {
        shape(c, {"kind", "raw"});
        const auto order = AlpacaPaperDecoder{}.order(c.at("raw").get<std::string>());
        const auto local = bind(order);
        // REST averages cannot supply missing executions. Keep routing closed until stream evidence and exact
        // reconciliation resolve the gap.
        if (order.filled_quantity.scaled_integer(0) != local.filled_shares)
            core_.disconnect();
        return {Outcome::applied, "REST identity recorded; no executions inferred from cumulative averages"};
    }
    if (kind == "reconcile") {
        shape(c, {"kind", "account", "positions", "orders"});
        require(authorized_ && subscribed_ && !unresolved_,
                "reconciliation requires authorized subscription and resolved evidence");
        const AlpacaPaperDecoder decoder{};
        const auto account = decoder.account(c.at("account").get<std::string>());
        require(account.id == configuration_.account_id && account.status == "ACTIVE" &&
                    !account.account_blocked && !account.trading_blocked,
                "wrong or blocked paper account");
        const auto positions = decoder.positions(c.at("positions").get<std::string>());
        const Shares shares = positions.empty() ? 0 : positions[0].quantity.scaled_integer(0);
        const auto orders = decoder.open_orders(c.at("orders").get<std::string>());
        BrokerSnapshot snapshot{nanos(account.cash.text()), shares, {}};
        for (const auto& order : orders) {
            const auto local = bind(order);
            require(!terminal(local.status), "broker reports a locally terminal order as open");
            snapshot.open_orders.push_back({local.request, order.filled_quantity.scaled_integer(0)});
        }
        if (!core_.account().connected)
            core_.reconnect();
        const auto outcome = core_.reconcile(snapshot);
        return {outcome, "exact account and complete open-order reconciliation; no balances overwritten"};
    }
    throw std::invalid_argument("unsupported paper command");
}
} // namespace trading::execution
