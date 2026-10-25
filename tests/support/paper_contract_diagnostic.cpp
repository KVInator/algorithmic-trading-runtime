#include "paper_contract_diagnostic.hpp"

#include <nlohmann/json.hpp>

#include <utility>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;

void check(PaperContractResult &result, std::string name, bool passed) {
    if (!passed) {
        throw std::logic_error("paper contract check failed: " + name);
    }
    result.checks.push_back({std::move(name), true});
}

Json order() {
    return {{"id", "11111111-1111-4111-8111-111111111111"},
            {"client_order_id", "MR1-1643734800-order"},
            {"asset_class", "us_equity"},
            {"symbol", "SPY"},
            {"type", "limit"},
            {"order_class", "simple"},
            {"side", "buy"},
            {"qty", "10"},
            {"filled_qty", "4"},
            {"limit_price", "100.01"},
            {"filled_avg_price", "100.005"},
            {"time_in_force", "day"},
            {"extended_hours", false},
            {"status", "partially_filled"},
            {"updated_at", "2026-10-26T10:00:00.123456789-04:00"}};
}

Json fill() {
    return {{"stream", "trade_updates"},
            {"data",
             {{"event", "partial_fill"},
              {"execution_id", "22222222-2222-4222-8222-222222222222"},
              {"order", order()},
              {"timestamp", "2026-10-26T14:00:00.123456789Z"},
              {"qty", "2"},
              {"price", "100.0075"},
              {"position_qty", "4"}}}};
}

class DiagnosticCases {
  public:
    explicit DiagnosticCases(PaperContractResult &result) : result_{result} {}

    template <class F> void examine(std::string name, const std::string &payload, bool accepted, F decode) {
        bool actual = true;
        std::string detail = "Decoded without runtime mutation";
        try {
            decode();
        } catch (const std::invalid_argument &error) {
            actual = false;
            detail = error.what();
        }
        check(result_, name, actual == accepted);
        result_.cases.push_back({std::move(name), payload, actual, std::move(detail)});
    }

  private:
    PaperContractResult &result_;
};
} // namespace

PaperContractResult PaperContractDiagnostic::run() const {
    PaperContractResult result{};
    DiagnosticCases cases{result};
    const AlpacaPaperRequests requests{};
    const execution::OrderRequest request{"MR1-1643734800-order", "SPY", execution::Side::buy, 10, 10001, 1};
    result.requests = {requests.submit(request),
                       requests.lookup(request.id),
                       requests.cancel("11111111-1111-4111-8111-111111111111"),
                       requests.account(),
                       requests.positions(),
                       requests.open_orders()};
    const auto submitted = Json::parse(result.requests[0].body);
    check(result, "explicit regular-hours DAY limit and stable client identity",
          submitted.at("time_in_force").get<std::string>() == "day" &&
              !submitted.at("extended_hours").get<bool>() &&
              submitted.at("client_order_id").get<std::string>() == request.id &&
              submitted.at("limit_price").get<std::string>() == "100.01");
    for (const auto &r : result.requests) {
        check(result, "exact paper origin: " + r.method + " " + r.url,
              r.url.starts_with("https://paper-api.alpaca.markets/"));
    }
    for (const auto url :
         {"https://api.alpaca.markets", "http://paper-api.alpaca.markets",
          "https://paper-api.alpaca.markets.evil.invalid", "https://paper-api.alpaca.markets/",
          "https://user@paper-api.alpaca.markets", "https://paper-api.alpaca.markets:443"}) {
        cases.examine(std::string{"reject REST origin: "} + url, url, false,
                      [&] { static_cast<void>(AlpacaPaperRequests{url}); });
    }
    cases.examine("reject live stream origin", "wss://api.alpaca.markets/stream", false, [&] {
        static_cast<void>(
            AlpacaPaperRequests{"https://paper-api.alpaca.markets", "wss://api.alpaca.markets/stream"});
    });
    for (const auto value : {"100.005", "100.0075", "0.01", "-0.0100", "100000.123456789", "000.000"}) {
        const BrokerDecimal decimal{value};
        std::optional<std::int64_t> cents;
        try {
            cents = decimal.scaled_integer(2);
        } catch (const std::invalid_argument &) {
        }
        result.decimals.push_back({value, decimal.text(), cents});
    }
    check(result, "subcent fills retain exact text",
          result.decimals[0].canonical == "100.005" && !result.decimals[0].cents &&
              result.decimals[1].canonical == "100.0075" && !result.decimals[1].cents);
    check(result, "cent and signed cash projections are exact",
          result.decimals[2].cents == 1 && result.decimals[3].cents == -1);
    check(result, "nanosecond offset timestamps identify the same instant",
          BrokerTimestamp{"2026-10-26T10:00:00.123456789-04:00"} ==
              BrokerTimestamp{"2026-10-26T14:00:00.123456789Z"});
    const AlpacaPaperDecoder decoder{};
    const BrokerTimestamp receipt{"2026-10-26T14:00:00.223456789Z"};
    const auto examine_stream = [&](std::string name, const Json &message, bool accepted) {
        const auto payload = message.dump();
        cases.examine(std::move(name), payload, accepted,
                      [&] { static_cast<void>(decoder.stream(payload, receipt)); });
    };
    examine_stream(
        "authorization success",
        {{"stream", "authorization"}, {"data", {{"status", "authorized"}, {"action", "authenticate"}}}},
        true);
    examine_stream(
        "authorization rejection",
        {{"stream", "authorization"}, {"data", {{"status", "unauthorized"}, {"action", "authenticate"}}}},
        true);
    examine_stream("subscription acknowledged",
                   {{"stream", "listening"}, {"data", {{"streams", Json::array({"trade_updates"})}}}}, true);
    examine_stream("subscription absent", {{"stream", "listening"}, {"data", {{"streams", Json::array()}}}},
                   true);
    auto message = fill();
    examine_stream("incremental partial fill with subcent price", message, true);
    const auto decoded = decoder.stream(message.dump(), receipt);
    const auto &update = *decoded.update;
    check(result, "incremental and cumulative quantities stay distinct",
          update.fill->quantity.text() == "2" && update.order.filled_quantity.text() == "4");
    check(result, "execution price is not cumulative average",
          update.fill->price.text() == "100.0075" && update.order.filled_average_price->text() == "100.005");
    check(result, "raw server payload and receipt clock retained",
          decoded.raw_payload == message.dump() && decoded.received_at == receipt);
    message["data"]["event"] = "fill";
    message["data"]["qty"] = "6";
    message["data"]["position_qty"] = "10";
    message["data"]["order"]["filled_qty"] = "10";
    message["data"]["order"]["status"] = "filled";
    examine_stream("complete fill retains incremental six-share execution", message, true);
    for (const auto event : {"accepted", "pending_new", "new", "pending_cancel", "canceled", "expired",
                             "rejected", "order_cancel_rejected", "done_for_day", "trade_bust"}) {
        auto value = fill();
        value["data"]["event"] = event;
        value["data"].erase("execution_id");
        value["data"].erase("qty");
        value["data"].erase("price");
        value["data"].erase("position_qty");
        examine_stream(std::string{"retained event: "} + event, value, true);
        if (std::string_view{event} == "done_for_day" || std::string_view{event} == "trade_bust") {
            check(result, "unsupported event requires later reconciliation",
                  !decoder.stream(value.dump(), receipt).update->recognized_event);
        }
    }
    for (const auto key : {"execution_id", "price", "qty", "timestamp"}) {
        auto value = fill();
        value["data"].erase(key);
        examine_stream(std::string{"reject missing fill field: "} + key, value, false);
    }
    message = fill();
    message["data"]["price"] = 100.0075;
    examine_stream("reject floating JSON monetary token", message, false);
    message = fill();
    message["data"]["qty"] = "5";
    examine_stream("reject incremental quantity above cumulative", message, false);
    message = fill();
    message["data"]["order"]["asset_class"] = "crypto";
    examine_stream("reject other asset class", message, false);
    message = fill();
    message["data"]["order"]["qty"] = "10.5";
    examine_stream("reject fractional requested quantity", message, false);
    message = fill();
    message["data"]["order"]["time_in_force"] = "gtc";
    examine_stream("reject unsupported TIF", message, false);
    message = fill();
    message["data"]["timestamp"] = "2026-10-26T14:00:00.1234567891Z";
    examine_stream("reject timestamp precision loss", message, false);
    const std::string duplicate =
        "{\"stream\":\"authorization\",\"stream\":\"listening\",\"data\":{\"streams\":[]}}";
    cases.examine("reject duplicate JSON fields", duplicate, false,
                  [&] { static_cast<void>(decoder.stream(duplicate, receipt)); });
    const auto account = Json{
        {"id", "33333333-3333-4333-8333-333333333333"},
        {"cash", "99999.970001"},
        {"currency", "USD"},
        {"status", "ACTIVE"},
        {"trading_blocked", false},
        {"account_blocked",
         false}}.dump();
    cases.examine("retain subcent independent cash", account, true, [&] {
        check(result, "subcent cash preserved", decoder.account(account).cash.text() == "99999.970001");
    });
    const auto orders = Json::array({order()}).dump();
    cases.examine("independent open-order response", orders, true,
                  [&] { static_cast<void>(decoder.open_orders(orders)); });
    const auto duplicate_orders = Json::array({order(), order()}).dump();
    cases.examine("reject duplicate open-order identities", duplicate_orders, false,
                  [&] { static_cast<void>(decoder.open_orders(duplicate_orders)); });
    const std::string saturated_orders = Json(500, order()).dump();
    cases.examine("reject potentially truncated open-order snapshot", saturated_orders, false,
                  [&] { static_cast<void>(decoder.open_orders(saturated_orders)); });
    const auto position = Json::array({{{"symbol", "SPY"},
                                        {"asset_class", "us_equity"},
                                        {"side", "long"},
                                        {"qty", "4"},
                                        {"avg_entry_price", "100.005"}}});
    cases.examine("retain subcent position cost", position.dump(), true,
                  [&] { static_cast<void>(decoder.positions(position.dump())); });
    auto foreign = position;
    foreign[0]["symbol"] = "AAPL";
    cases.examine("reject untracked position", foreign.dump(), false,
                  [&] { static_cast<void>(decoder.positions(foreign.dump())); });
    return result;
}
} // namespace trading
