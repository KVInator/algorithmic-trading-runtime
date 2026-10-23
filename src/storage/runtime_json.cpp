#include "runtime_json.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace trading::execution {
using Json = nlohmann::json;
namespace {
Json order_json(const OrderRequest& order) {
    return {{"id", order.id}, {"symbol", order.symbol}, {"side", name(order.side)}, {"shares", order.shares},
            {"limit_cents", order.limit_cents}, {"fee_per_share_ceiling_cents", order.fee_per_share_ceiling_cents}};
}
std::int64_t integer(const Json& value) {
    if (!value.is_number_integer() || (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
        throw std::invalid_argument("runtime JSON requires representable integer units");
    }
    return value.get<std::int64_t>();
}
OrderRequest parse_order(const Json& value) {
    const auto side = value.at("side").get<std::string>();
    if (side != "buy" && side != "sell") { throw std::invalid_argument("unknown persisted order side"); }
    return {value.at("id").get<std::string>(), value.at("symbol").get<std::string>(),
            side == "buy" ? Side::buy : Side::sell, integer(value.at("shares")), integer(value.at("limit_cents")),
            integer(value.at("fee_per_share_ceiling_cents"))};
}
} // namespace
Json input_json(const RuntimeInput& input) {
    return std::visit([](const auto& item) -> Json {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, SubmitInput>) {
            return {{"kind", "submit"}, {"order", order_json(item.request)}};
        } else if constexpr (std::is_same_v<T, CancelInput>) {
            return {{"kind", "cancel"}, {"order_id", item.order_id}, {"cancel_id", item.cancel_id}};
        } else if constexpr (std::is_same_v<T, ConnectionInput>) {
            return {{"kind", item.connected ? "reconnect" : "disconnect"}};
        } else if constexpr (std::is_same_v<T, TargetIntent>) {
            return {{"kind", "target"}, {"id", item.id}, {"symbol", item.symbol},
                {"decision_at_seconds", item.decision_at_seconds}, {"expires_at_seconds", item.expires_at_seconds},
                {"target_shares", item.target_shares}, {"buy_limit_cents", item.buy_limit_cents},
                {"sell_limit_cents", item.sell_limit_cents}, {"fee_per_share_ceiling_cents", item.fee_per_share_ceiling_cents}};
        } else if constexpr (std::is_same_v<T, BrokerSnapshot>) {
            Json orders = Json::array();
            for (const auto& order : item.open_orders) {
                orders.push_back({{"order", order_json(order.request)}, {"filled_shares", order.filled_shares}});
            }
            return {{"kind", "reconcile"}, {"cash_cents", item.cash_cents}, {"position_shares", item.position_shares},
                    {"open_orders", std::move(orders)}};
        } else {
            return {{"kind", "report"}, {"report_kind", name(item.kind)}, {"event_id", item.event_id},
                    {"order_id", item.order_id}, {"execution_id", item.execution_id}, {"fill_shares", item.fill_shares},
                    {"fill_price_cents", item.fill_price_cents}, {"fee_cents", item.fee_cents},
                    {"cumulative_filled_shares", item.cumulative_filled_shares}, {"cancel_id", item.cancel_id}};
        }
    }, input);
}
Json account_json(const AccountSnapshot& account) {
    return {{"cash_cents", account.cash_cents}, {"position_shares", account.position_shares},
            {"reserved_cash_cents", account.reserved_cash_cents}, {"reserved_sell_shares", account.reserved_sell_shares},
            {"outstanding_buy_shares", account.outstanding_buy_shares}, {"open_orders", account.open_orders},
            {"connected", account.connected}, {"reconciliation_required", account.reconciliation_required}};
}

RuntimeInput parse_runtime_input(const Json& value) {
    const auto kind = value.at("kind").get<std::string>();
    RuntimeInput input;
    if (kind == "submit") { input = SubmitInput{parse_order(value.at("order"))}; }
    else if (kind == "cancel") {
        input = CancelInput{value.at("order_id").get<std::string>(), value.at("cancel_id").get<std::string>()};
    } else if (kind == "disconnect" || kind == "reconnect") { input = ConnectionInput{kind == "reconnect"}; }
    else if (kind == "reconcile") {
        BrokerSnapshot snapshot{integer(value.at("cash_cents")), integer(value.at("position_shares")), {}};
        if (!value.at("open_orders").is_array()) { throw std::invalid_argument("persisted open orders must be an array"); }
        for (const auto& order : value.at("open_orders")) {
            snapshot.open_orders.push_back({parse_order(order.at("order")), integer(order.at("filled_shares"))});
        }
        input = std::move(snapshot);
    } else if (kind == "target") {
        input = TargetIntent{value.at("id").get<std::string>(), value.at("symbol").get<std::string>(),
            integer(value.at("decision_at_seconds")), integer(value.at("expires_at_seconds")),
            integer(value.at("target_shares")), integer(value.at("buy_limit_cents")),
            integer(value.at("sell_limit_cents")), integer(value.at("fee_per_share_ceiling_cents"))};
    } else if (kind == "report") {
        const auto report_kind = value.at("report_kind").get<std::string>();
        ReportKind type;
        if (report_kind == "accepted") { type = ReportKind::accepted; }
        else if (report_kind == "fill") { type = ReportKind::fill; }
        else if (report_kind == "canceled") { type = ReportKind::canceled; }
        else if (report_kind == "rejected") { type = ReportKind::rejected; }
        else if (report_kind == "cancel_rejected") { type = ReportKind::cancel_rejected; }
        else { throw std::invalid_argument("unknown persisted broker report kind"); }
        input = BrokerReport{value.at("event_id").get<std::string>(), value.at("order_id").get<std::string>(), type,
            value.at("execution_id").get<std::string>(), integer(value.at("fill_shares")),
            integer(value.at("fill_price_cents")), integer(value.at("fee_cents")),
            integer(value.at("cumulative_filled_shares")), value.at("cancel_id").get<std::string>()};
    } else { throw std::invalid_argument("unknown persisted runtime input kind"); }
    if (input_json(input).dump() != value.dump()) { throw std::invalid_argument("runtime input schema differs from its canonical shape"); }
    return input;
}
Json trace_json(const TraceEntry& trace) {
    return {{"sequence", trace.sequence}, {"input", trace.input}, {"order_id", trace.order_id},
            {"event_id", trace.event_id}, {"outcome", name(trace.outcome)}, {"reason", trace.reason},
            {"order_status", trace.order_status}, {"account", account_json(trace.account)}};
}

} // namespace trading::execution
