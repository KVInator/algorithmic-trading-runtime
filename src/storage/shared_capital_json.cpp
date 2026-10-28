#include "shared_capital_json.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
MoneyUnits integer(const Json& v) {
    if (!v.is_number_integer() || (v.is_number_unsigned() && v.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<MoneyUnits>::max()))) {
        throw std::invalid_argument("schema 4 requires signed representable integer units");
    }
    return v.get<MoneyUnits>();
}
Json key_json(const OwnerKey& key) { return {{"family", key.family}, {"symbol", key.symbol}}; }
OwnerKey parse_key(const Json& j) { return {j.at("family").get<std::string>(), j.at("symbol").get<std::string>()}; }
Json order_json(const OrderRequest& o) {
    return {{"id", o.id}, {"symbol", o.symbol}, {"side", name(o.side)}, {"shares", o.shares},
        {"limit_units", o.limit_units}, {"fee_per_share_ceiling_units", o.fee_per_share_ceiling_units}};
}
OrderRequest parse_order(const Json& j) {
    const auto side = j.at("side").get<std::string>();
    if (side != "buy" && side != "sell") { throw std::invalid_argument("invalid shared order side"); }
    return {j.at("id").get<std::string>(), j.at("symbol").get<std::string>(), side == "buy" ? Side::buy : Side::sell,
        integer(j.at("shares")), integer(j.at("limit_units")), integer(j.at("fee_per_share_ceiling_units"))};
}
Json quote_json(const SharedQuote& q) { return {{"symbol", q.symbol}, {"source_at_seconds", q.source_at_seconds}, {"bid_units", q.bid_units}, {"ask_units", q.ask_units}}; }
Json report_json(const BrokerReport& r) {
    return {{"event_id", r.event_id}, {"order_id", r.order_id}, {"report_kind", name(r.kind)}, {"execution_id", r.execution_id},
        {"fill_shares", r.fill_shares}, {"fill_price_units", r.fill_price_units}, {"fee_units", r.fee_units},
        {"cumulative_filled_shares", r.cumulative_filled_shares}, {"cancel_id", r.cancel_id}};
}
BrokerReport parse_report(const Json& j) {
    std::optional<ReportKind> kind;
    for (const auto value : {ReportKind::accepted, ReportKind::fill, ReportKind::canceled, ReportKind::rejected, ReportKind::cancel_rejected}) {
        if (j.at("report_kind").get<std::string>() == name(value)) { kind = value; }
    }
    if (!kind) { throw std::invalid_argument("invalid shared report kind"); }
    return {j.at("event_id").get<std::string>(), j.at("order_id").get<std::string>(), *kind, j.at("execution_id").get<std::string>(),
        integer(j.at("fill_shares")), integer(j.at("fill_price_units")), integer(j.at("fee_units")), integer(j.at("cumulative_filled_shares")), j.at("cancel_id").get<std::string>()};
}
}
Json shared_configuration_json(const SharedCapitalConfiguration& c) {
    auto symbols = c.symbols, families = c.families;
    std::sort(symbols.begin(), symbols.end()); std::sort(families.begin(), families.end());
    return {{"symbols", symbols}, {"families", families}, {"initial_cash_units", c.initial_cash_units},
        {"usd_scale", shared_usd_scale}, {"fee_per_share_ceiling_units", c.fee_per_share_ceiling_units},
        {"maximum_quote_age_seconds", c.maximum_quote_age_seconds}, {"dispatch_window_seconds", c.dispatch_window_seconds},
        {"owner_budget_denominator", 15}, {"family_budget_denominator", 3}, {"instrument_budget_denominator", 5}};
}
Json shared_input_json(const SharedInput& input) {
    return std::visit([](const auto& i) -> Json {
        using T = std::decay_t<decltype(i)>;
        Json j{{"at_seconds", i.at_seconds}};
        if constexpr (std::is_same_v<T, SharedTargetBatch>) {
            j["kind"] = "targets"; j["id"] = i.id; j["quotes"] = Json::array(); j["signals"] = Json::array();
            for (const auto& q : i.quotes) { j["quotes"].push_back(quote_json(q)); }
            for (const auto& s : i.signals) { auto value = key_json(s.owner); value["long_target"] = s.long_target; j["signals"].push_back(value); }
        } else if constexpr (std::is_same_v<T, SharedDelivery>) { j["kind"] = "report"; j["report"] = report_json(i.report); }
        else if constexpr (std::is_same_v<T, SharedConnection>) { j["kind"] = i.connected ? "reconnect" : "disconnect"; }
        else if constexpr (std::is_same_v<T, SharedReconciliation>) {
            j["kind"] = "reconcile"; j["cash_units"] = i.broker.cash_units; j["positions"] = i.broker.positions; j["open_orders"] = Json::array();
            for (const auto& order : i.broker.open_orders) { j["open_orders"].push_back({{"order", order_json(order.request)}, {"filled_shares", order.filled_shares}}); }
        } else { j["kind"] = "seal"; }
        return j;
    }, input);
}
SharedInput parse_shared_input(const Json& j) {
    const auto kind = j.at("kind").get<std::string>(); const auto at = integer(j.at("at_seconds"));
    SharedInput result;
    if (kind == "targets") {
        SharedTargetBatch batch{j.at("id").get<std::string>(), at, {}, {}};
        for (const auto& q : j.at("quotes")) { batch.quotes.push_back({q.at("symbol").get<std::string>(), integer(q.at("source_at_seconds")), integer(q.at("bid_units")), integer(q.at("ask_units"))}); }
        for (const auto& s : j.at("signals")) { batch.signals.push_back({parse_key(s), s.at("long_target").get<bool>()}); }
        result = std::move(batch);
    } else if (kind == "report") { result = SharedDelivery{at, parse_report(j.at("report"))}; }
    else if (kind == "disconnect" || kind == "reconnect") { result = SharedConnection{at, kind == "reconnect"}; }
    else if (kind == "reconcile") {
        PortfolioBrokerSnapshot broker{integer(j.at("cash_units")), {}, {}};
        if (!j.at("positions").is_object()) { throw std::invalid_argument("positions must be a keyed object"); }
        for (const auto& [symbol, q] : j.at("positions").items()) { broker.positions.emplace(symbol, integer(q)); }
        for (const auto& o : j.at("open_orders")) { broker.open_orders.push_back({parse_order(o.at("order")), integer(o.at("filled_shares"))}); }
        result = SharedReconciliation{at, std::move(broker)};
    } else if (kind == "seal") { result = SharedSeal{at}; }
    else { throw std::invalid_argument("unknown shared-capital command"); }
    if (shared_input_json(result) != j) { throw std::invalid_argument("noncanonical shared input fields or types"); }
    return result;
}
Json shared_result_json(const SharedResult& r) {
    Json j{{"outcome", name(r.outcome)}, {"reason", r.reason}, {"orders", Json::array()}, {"cancels", Json::array()}, {"fills", Json::array()}, {"transfers", Json::array()}};
    for (const auto& o : r.dispatch_orders) { j["orders"].push_back(order_json(o)); }
    for (const auto& [id, cancel] : r.dispatch_cancels) { j["cancels"].push_back({{"order_id", id}, {"cancel_id", cancel}}); }
    for (const auto& fill : r.fills) { auto value = key_json(fill.owner); value["shares"] = fill.shares; value["fee_units"] = fill.fee_units; j["fills"].push_back(value); }
    for (const auto& t : r.transfers) { j["transfers"].push_back({{"from", key_json(t.from)}, {"to", key_json(t.to)}, {"shares", t.shares}, {"price_units", t.price_units}}); }
    return j;
}
Json shared_snapshot_json(const SharedCapitalSnapshot& s) {
    Json j{{"cash_units", s.account.cash_units}, {"reserved_cash_units", s.account.reserved_cash_units}, {"equity_units", s.equity_units},
        {"connected", s.account.connected}, {"reconciliation_required", s.account.reconciliation_required}, {"positions", Json::array()},
        {"owners", Json::array()}, {"quotes", Json::array()}, {"open_orders", Json::array()}, {"budget_breaches", s.budget_breaches},
        {"at_seconds", s.at_seconds}, {"generation", s.generation}, {"sealed", s.sealed}};
    for (const auto& [symbol, position] : s.account.positions) { j["positions"].push_back({{"symbol", symbol}, {"shares", position.shares}, {"reserved_sell_shares", position.reserved_sell_shares}, {"outstanding_buy_shares", position.outstanding_buy_shares}}); }
    for (const auto& [key, owner] : s.owners) {
        auto value = key_json(key); value["shares"] = owner.shares; value["desired_shares"] = owner.desired_shares;
        value["cash_flow_units"] = owner.attribution_cash_flow_units; value["decision_at_seconds"] = owner.decision_at_seconds; value["expires_at_seconds"] = owner.expires_at_seconds;
        j["owners"].push_back(value);
    }
    for (const auto& [symbol, q] : s.quotes) { static_cast<void>(symbol); j["quotes"].push_back(quote_json(q)); }
    for (const auto& order : s.open_orders) {
        Json claims = Json::array();
        for (const auto& [key, q] : s.open_plans.at(order.request.id).remaining) { auto value = key_json(key); value["remaining"] = q; claims.push_back(value); }
        j["open_orders"].push_back({{"order", order_json(order.request)}, {"filled_shares", order.filled_shares}, {"status", name(order.status)},
            {"acknowledged", order.acknowledged}, {"pending_cancel_id", order.pending_cancel_id}, {"claims", claims}});
    }
    return j;
}
Json shared_checkpoint_json(const SharedFlatCheckpoint& c) {
    Json flows = Json::array();
    for (const auto& [key, flow] : c.attribution_cash_flows) { auto row = key_json(key); row["cash_flow_units"] = flow; flows.push_back(row); }
    return {{"cash_units", c.cash_units}, {"at_seconds", c.at_seconds}, {"owner_cash_flows", flows}};
}
SharedFlatCheckpoint parse_shared_checkpoint(const Json& j) {
    SharedFlatCheckpoint c{integer(j.at("cash_units")), {}, integer(j.at("at_seconds"))};
    for (const auto& row : j.at("owner_cash_flows")) { if (!c.attribution_cash_flows.emplace(parse_key(row), integer(row.at("cash_flow_units"))).second) { throw std::invalid_argument("duplicate checkpoint owner"); } }
    if (shared_checkpoint_json(c) != j) { throw std::invalid_argument("noncanonical flat checkpoint"); }
    return c;
}
Json broker_order_json(const OrderRequest& order) { return order_json(order); }
OrderRequest parse_broker_order(const Json& value) {
    const auto order = parse_order(value);
    if (order_json(order) != value) { throw std::invalid_argument("noncanonical broker order fields"); }
    return order;
}
Json broker_report_json(const BrokerReport& report) { return report_json(report); }
BrokerReport parse_broker_report(const Json& value) {
    const auto report = parse_report(value);
    if (report_json(report) != value) { throw std::invalid_argument("noncanonical broker report fields"); }
    return report;
}
} // namespace trading::execution
