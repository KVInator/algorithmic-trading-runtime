#include "alpaca_paper_contract.hpp"

#include "json_input.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include <set>

namespace trading {
namespace {
using Json = nlohmann::json;

void client_id(std::string_view value) {
    if (value.empty() || value.size() > 48 || !std::all_of(value.begin(), value.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                   c == '-';
        })) {
        throw std::invalid_argument("client order ID is outside the local ASCII/48-character subset");
    }
}

std::string uuid(std::string_view value) {
    if (value.size() != 36) {
        throw std::invalid_argument("broker identity must be a UUID");
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                throw std::invalid_argument("invalid broker UUID separator");
            }
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            throw std::invalid_argument("invalid broker UUID digit");
        }
    }
    std::string normalized{value};
    for (auto& c : normalized) {
        if (c >= 'A' && c <= 'F') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return normalized;
}

const Json& field(const Json& item, const char* name) {
    if (!item.is_object() || !item.contains(name)) {
        throw std::invalid_argument(std::string{"missing broker field: "} + name);
    }
    return item.at(name);
}

std::string text(const Json& item, const char* name) {
    const auto& value = field(item, name);
    if (!value.is_string() || value.get_ref<const std::string&>().size() > 256) {
        throw std::invalid_argument(std::string{"broker field requires bounded text: "} + name);
    }
    return value.get<std::string>();
}

bool flag(const Json& item, const char* name) {
    const auto& value = field(item, name);
    if (!value.is_boolean()) {
        throw std::invalid_argument(std::string{"broker field requires boolean: "} + name);
    }
    return value.get<bool>();
}

Json document(std::string_view payload) {
    if (payload.empty() || payload.size() > 1024U * 1024U) {
        throw std::invalid_argument("paper payload exceeds one MiB or is empty");
    }
    return parse_json_document(payload);
}

std::optional<BrokerDecimal> optional_decimal(const Json& item, const char* name) {
    const auto& value = field(item, name);
    if (value.is_null()) {
        return std::nullopt;
    }
    return BrokerDecimal{text(item, name)};
}

PaperOrder decode_order(const Json& item) {
    auto id = text(item, "id"), client = text(item, "client_order_id");
    id = uuid(id);
    client_id(client);
    if (text(item, "asset_class") != "us_equity" || text(item, "symbol") != "SPY" ||
        text(item, "type") != "limit" || text(item, "time_in_force") != "day" ||
        flag(item, "extended_hours")) {
        throw std::invalid_argument("paper order is outside SPY regular-hours DAY limits");
    }
    const auto order_class = text(item, "order_class");
    if (order_class != "simple" && !order_class.empty()) {
        throw std::invalid_argument("complex paper orders are unsupported");
    }
    if (item.contains("legs") && !item.at("legs").is_null() &&
        (!item.at("legs").is_array() || !item.at("legs").empty())) {
        throw std::invalid_argument("paper order legs are unsupported");
    }
    if (item.contains("notional") && !item.at("notional").is_null()) {
        throw std::invalid_argument("notional paper orders are unsupported");
    }
    const auto side = text(item, "side");
    if (side != "buy" && side != "sell") {
        throw std::invalid_argument("invalid paper order side");
    }
    BrokerDecimal quantity{text(item, "qty")}, filled{text(item, "filled_qty")},
        price{text(item, "limit_price")};
    auto average = optional_decimal(item, "filled_avg_price");
    if (quantity <= BrokerDecimal{"0"} || quantity.scaled_integer(0) <= 0 || filled < BrokerDecimal{"0"} ||
        filled > quantity || price < BrokerDecimal{"1"} || price.scaled_integer(2) <= 0 ||
        (filled > BrokerDecimal{"0"}) != average.has_value() || (average && *average <= BrokerDecimal{"0"})) {
        throw std::invalid_argument("inconsistent paper order quantities/prices");
    }
    const auto status = text(item, "status");
    if (status.empty() || (status == "filled" && filled != quantity)) {
        throw std::invalid_argument("inconsistent paper order status");
    }
    return {std::move(id),
            std::move(client),
            "SPY",
            side == "buy" ? execution::Side::buy : execution::Side::sell,
            std::move(quantity),
            std::move(filled),
            std::move(price),
            std::move(average),
            status,
            BrokerTimestamp{text(item, "updated_at")}};
}
} // namespace

AlpacaPaperRequests::AlpacaPaperRequests(std::string_view rest, std::string_view stream)
    : rest_{"https://paper-api.alpaca.markets"}, stream_{"wss://paper-api.alpaca.markets/stream"} {
    if (rest != "https://paper-api.alpaca.markets" || stream != "wss://paper-api.alpaca.markets/stream") {
        throw std::invalid_argument("only the exact Alpaca paper REST and stream endpoints are permitted");
    }
}

PaperRequest AlpacaPaperRequests::submit(const execution::OrderRequest& order) const {
    client_id(order.id);
    if (order.symbol != "SPY" || order.shares <= 0 || order.limit_units < 100 ||
        order.fee_per_share_ceiling_units < 0 ||
        (order.side != execution::Side::buy && order.side != execution::Side::sell)) {
        throw std::invalid_argument("paper submission requires a positive SPY whole-share limit order");
    }
    const auto cents = std::to_string(order.limit_units % 100);
    const auto price = std::to_string(order.limit_units / 100) + '.' + (cents.size() == 1 ? "0" : "") + cents;
    return {"POST", rest_ + "/v2/orders",
            Json{{"client_order_id", order.id},
                 {"symbol", order.symbol},
                 {"qty", std::to_string(order.shares)},
                 {"side", execution::name(order.side)},
                 {"type", "limit"},
                 {"limit_price", price},
                 {"time_in_force", "day"},
                 {"order_class", "simple"},
                 {"extended_hours", false}}
                .dump()};
}

PaperRequest AlpacaPaperRequests::lookup(std::string_view id) const {
    client_id(id);
    return {"GET", rest_ + "/v2/orders:by_client_order_id?client_order_id=" + std::string{id}, ""};
}

PaperRequest AlpacaPaperRequests::cancel(std::string_view id) const {
    return {"DELETE", rest_ + "/v2/orders/" + uuid(id), ""};
}

PaperRequest AlpacaPaperRequests::account() const {
    return {"GET", rest_ + "/v2/account", ""};
}

PaperRequest AlpacaPaperRequests::positions() const {
    return {"GET", rest_ + "/v2/positions", ""};
}

PaperRequest AlpacaPaperRequests::open_orders() const {
    return {"GET", rest_ + "/v2/orders?status=open&limit=500&direction=asc&nested=false", ""};
}

const std::string& AlpacaPaperRequests::stream_url() const noexcept {
    return stream_;
}

PaperOrder AlpacaPaperDecoder::order(std::string_view payload) const {
    return decode_order(document(payload));
}

PaperAccount AlpacaPaperDecoder::account(std::string_view payload) const {
    const auto item = document(payload);
    const auto id = uuid(text(item, "id"));
    if (text(item, "currency") != "USD") {
        throw std::invalid_argument("paper account currency must be USD");
    }
    return {id, BrokerDecimal{text(item, "cash")}, text(item, "status"), flag(item, "trading_blocked"),
            flag(item, "account_blocked")};
}

std::vector<PaperOrder> AlpacaPaperDecoder::open_orders(std::string_view payload) const {
    const auto items = document(payload);
    // Reaching the requested API limit cannot establish complete coverage.
    if (!items.is_array() || items.size() >= 500) {
        throw std::invalid_argument("open-order response is invalid or potentially truncated");
    }
    std::vector<PaperOrder> result;
    std::set<std::string> broker_ids, client_ids;
    for (const auto& item : items) {
        auto value = decode_order(item);
        if (!broker_ids.insert(value.broker_id).second || !client_ids.insert(value.client_id).second ||
            value.status == "filled" || value.status == "canceled" || value.status == "rejected" ||
            value.status == "expired") {
            throw std::invalid_argument("duplicate or terminal order in open-order response");
        }
        result.push_back(std::move(value));
    }
    return result;
}

std::vector<PaperPosition> AlpacaPaperDecoder::positions(std::string_view payload) const {
    const auto items = document(payload);
    if (!items.is_array() || items.size() > 500) {
        throw std::invalid_argument("paper positions require a bounded array");
    }
    std::vector<PaperPosition> result;
    for (const auto& item : items) {
        if (text(item, "symbol") != "SPY" || text(item, "asset_class") != "us_equity" ||
            text(item, "side") != "long" || !result.empty()) {
            throw std::invalid_argument("untracked, duplicate or short paper position");
        }
        BrokerDecimal qty{text(item, "qty")}, price{text(item, "avg_entry_price")};
        if (qty <= BrokerDecimal{"0"} || price <= BrokerDecimal{"0"}) {
            throw std::invalid_argument("invalid paper position");
        }
        result.push_back({"SPY", std::move(qty), std::move(price)});
    }
    return result;
}

PaperStreamMessage AlpacaPaperDecoder::stream(std::string_view payload,
                                              const BrokerTimestamp& received_at) const {
    const auto item = document(payload);
    const auto name = text(item, "stream");
    const auto& data = field(item, "data");
    PaperStreamMessage result{
        std::string{payload}, received_at, PaperStreamKind::trade_update, false, false, {}};
    if (name == "authorization") {
        const auto status = text(data, "status");
        if (status != "authorized" && status != "unauthorized") {
            throw std::invalid_argument("unknown paper authorization status");
        }
        result.kind = PaperStreamKind::authorization;
        result.authorized = status == "authorized";
    } else if (name == "listening") {
        const auto& streams = field(data, "streams");
        if (!streams.is_array() || streams.size() > 16) {
            throw std::invalid_argument("invalid paper stream subscription response");
        }
        for (const auto& s : streams) {
            if (!s.is_string()) {
                throw std::invalid_argument("invalid paper stream name");
            }
            if (s.get<std::string>() == "trade_updates") {
                result.trade_updates_subscribed = true;
            }
        }
        result.kind = PaperStreamKind::listening;
    } else if (name == "trade_updates") {
        const auto event = text(data, "event");
        static constexpr std::array supported{"accepted", "pending_new",          "new",      "partial_fill",
                                              "fill",     "pending_cancel",       "canceled", "expired",
                                              "rejected", "order_cancel_rejected"};
        PaperTradeUpdate update{
            event, {}, decode_order(field(data, "order")),
            {},    {}, std::find(supported.begin(), supported.end(), event) != supported.end()};
        if (data.contains("event_id")) {
            update.event_id = text(data, "event_id");
            if (update.event_id->empty()) {
                throw std::invalid_argument("empty broker event identity");
            }
        }
        if (data.contains("timestamp") && !data.at("timestamp").is_null()) {
            update.timestamp = BrokerTimestamp{text(data, "timestamp")};
        }
        if (event == "fill" || event == "partial_fill") {
            const auto execution = uuid(text(data, "execution_id"));
            PaperFill fill{execution, BrokerDecimal{text(data, "qty")}, BrokerDecimal{text(data, "price")},
                           BrokerDecimal{text(data, "position_qty")}};
            if (!update.timestamp || fill.quantity <= BrokerDecimal{"0"} ||
                fill.quantity > update.order.filled_quantity || fill.price <= BrokerDecimal{"0"} ||
                fill.position_quantity < BrokerDecimal{"0"} ||
                (event == "fill" && update.order.status != "filled") ||
                (event == "partial_fill" && (update.order.status != "partially_filled" ||
                                             update.order.filled_quantity >= update.order.quantity))) {
                throw std::invalid_argument("inconsistent paper execution message");
            }
            update.fill = std::move(fill);
        }
        result.update = std::move(update);
    } else {
        throw std::invalid_argument("unsupported paper stream envelope");
    }
    return result;
}
} // namespace trading
