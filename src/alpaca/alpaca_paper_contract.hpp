#pragma once

#include "broker_value.hpp"
#include "order_runtime.hpp"

#include <optional>
#include <vector>

namespace trading {

struct PaperRequest {
    std::string method;
    std::string url;
    std::string body;
};

// A request description, not a transport. Only these exact paper origins exist.
class AlpacaPaperRequests {
public:
    explicit AlpacaPaperRequests(std::string_view rest = "https://paper-api.alpaca.markets",
                                 std::string_view stream = "wss://paper-api.alpaca.markets/stream");
    [[nodiscard]] PaperRequest submit(const execution::OrderRequest& order) const;
    [[nodiscard]] PaperRequest lookup(std::string_view client_order_id) const;
    [[nodiscard]] PaperRequest cancel(std::string_view broker_order_id) const;
    [[nodiscard]] PaperRequest account() const;
    [[nodiscard]] PaperRequest positions() const;
    [[nodiscard]] PaperRequest open_orders() const;
    [[nodiscard]] const std::string& stream_url() const noexcept;

private:
    std::string rest_;
    std::string stream_;
};

struct PaperOrder {
    std::string broker_id;
    std::string client_id;
    std::string symbol;
    execution::Side side;
    BrokerDecimal quantity;
    BrokerDecimal filled_quantity;
    BrokerDecimal limit_price;
    std::optional<BrokerDecimal> filled_average_price;
    std::string status;
    BrokerTimestamp updated_at;
};

struct PaperFill {
    std::string execution_id;
    BrokerDecimal quantity; // This execution, not order.filled_qty.
    BrokerDecimal price;    // This execution, not order.filled_avg_price.
    BrokerDecimal position_quantity;
};

struct PaperTradeUpdate {
    std::string event;
    std::optional<std::string> event_id;
    PaperOrder order;
    std::optional<BrokerTimestamp> timestamp;
    std::optional<PaperFill> fill;
    // Unknown or unsupported events retain raw input and require reconciliation.
    bool recognized_event;
};

enum class PaperStreamKind { authorization, listening, trade_update };

struct PaperStreamMessage {
    std::string raw_payload;
    BrokerTimestamp received_at;
    PaperStreamKind kind;
    bool authorized{false};
    bool trade_updates_subscribed{false};
    std::optional<PaperTradeUpdate> update;
};

struct PaperAccount {
    std::string id;
    BrokerDecimal cash;
    std::string status;
    bool trading_blocked;
    bool account_blocked;
};

struct PaperPosition {
    std::string symbol;
    BrokerDecimal quantity;
    BrokerDecimal average_entry_price;
};

// Decodes the bounded SPY, USD, simple DAY-limit subset. No runtime mutation,
// fee inference, cash reconciliation or automatic conversion to cent reports.
class AlpacaPaperDecoder {
public:
    [[nodiscard]] PaperOrder order(std::string_view payload) const;
    [[nodiscard]] PaperAccount account(std::string_view payload) const;
    [[nodiscard]] std::vector<PaperOrder> open_orders(std::string_view payload) const;
    [[nodiscard]] std::vector<PaperPosition> positions(std::string_view payload) const;
    [[nodiscard]] PaperStreamMessage stream(std::string_view payload,
                                            const BrokerTimestamp& received_at) const;
};

} // namespace trading
