#include "paper_contract_diagnostic.hpp"

#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace trading;
using Json = nlohmann::json;

void require(bool condition, std::string_view name) {
    if (!condition) {
        throw std::runtime_error(std::string{name});
    }
}

template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "unsupported input must fail without lossy conversion");
}

void values() {
    require(BrokerDecimal{"-0001.2500"}.text() == "-1.25" && BrokerDecimal{"-0.000"}.text() == "0",
            "canonical exact decimals");
    require(BrokerDecimal{"100.005"} > BrokerDecimal{"100.004999999999999999"} &&
                BrokerDecimal{"-100.005"} < BrokerDecimal{"-100.004"},
            "exact signed decimal ordering");
    require(BrokerDecimal{"9.99"} < BrokerDecimal{"10"} && BrokerDecimal{"1.0"} == BrokerDecimal{"01.000"},
            "whole/fraction comparison");
    require(BrokerDecimal{"100.0075"}.scaled_integer(4) == 1000075, "subcent preservation");
    require(BrokerDecimal{"92233720368547758.07"}.scaled_integer(2) ==
                std::numeric_limits<std::int64_t>::max(),
            "positive integer boundary");
    require(BrokerDecimal{"-92233720368547758.08"}.scaled_integer(2) ==
                std::numeric_limits<std::int64_t>::min(),
            "negative integer boundary");
    for (const auto x : {"", "-", "--1", "+1", "1e2", "NaN", "1.", ".1", " 1", "1 0", "1.2.3", "1\n"}) {
        rejects([&] { static_cast<void>(BrokerDecimal{x}); });
    }
    for (const auto x : {"92233720368547758.08", "-92233720368547758.09", "100.005"}) {
        rejects([&] { static_cast<void>(BrokerDecimal{x}.scaled_integer(2)); });
    }
    rejects([&] { static_cast<void>(BrokerDecimal{"1"}.scaled_integer(19)); });
    const BrokerTimestamp a{"2026-10-26T10:00:00.123456789-04:00"}, b{"2026-10-26T14:00:00.123456789Z"};
    require(a == b && a.text() == "2026-10-26T10:00:00.123456789-04:00",
            "instant and original broker timestamp retained");
    require((BrokerTimestamp{"2026-10-26T14:00:00.123456790Z"}.value() - b.value()).count() == 1,
            "one nanosecond is not truncated");
    require(BrokerTimestamp{"2026-10-26T00:00:00+14:00"} == BrokerTimestamp{"2026-10-25T10:00:00Z"},
            "offset can cross date boundary");
    for (const auto x : {"2026-10-26T14:00:00.1234567891Z", "2026-02-29T14:00:00Z", "2026-10-26T14:00:60Z",
                         "2026-10-26T14:00:00-00:00", "2026-10-26T14:00:00+14:01", "2026-10-26T14:00:00.Z",
                         "2026-10-26T14:00:00", "2200-01-01T00:00:00Z"}) {
        rejects([&] { static_cast<void>(BrokerTimestamp{x}); });
    }
}

void requests() {
    const AlpacaPaperRequests api{};
    execution::OrderRequest order{"IM1-1-order", "SPY", execution::Side::buy, 7, 10001, 1};
    const auto r = api.submit(order);
    const auto body = Json::parse(r.body);
    require(r.method == "POST" && r.url == "https://paper-api.alpaca.markets/v2/orders", "paper POST origin");
    require(body.at("qty").get<std::string>() == "7" &&
                body.at("limit_price").get<std::string>() == "100.01" &&
                body.at("order_class").get<std::string>() == "simple",
            "money and quantity are exact wire strings");
    for (const auto id : {"bad&id=2", "bad/id", "bad id", "", "bad\nheader"}) {
        rejects([&] { static_cast<void>(api.lookup(id)); });
    }
    rejects([&] { static_cast<void>(api.lookup(std::string(49, 'x'))); });
    rejects([&] { static_cast<void>(api.cancel("/v2/account")); });
    require(api.cancel("ABCDEFAB-ABCD-4BCD-8BCD-ABCDEFABCDEF")
                .url.ends_with("abcdefab-abcd-4bcd-8bcd-abcdefabcdef"),
            "UUID identities normalize hex case");
    order.symbol = "AAPL";
    rejects([&] { static_cast<void>(api.submit(order)); });
    order.symbol = "SPY";
    order.limit_units = 99;
    rejects([&] { static_cast<void>(api.submit(order)); });
}
} // namespace

int main() {
    try {
        values();
        requests();
        const auto result = PaperContractDiagnostic{}.run();
        const AlpacaPaperDecoder decoder{};
        const BrokerTimestamp receipt{"2026-10-26T14:00:00Z"};
        for (const auto &c : result.cases) {
            if (c.name == "authorization rejection") {
                require(!decoder.stream(c.payload, receipt).authorized, "auth denial retained");
            }
            if (c.name == "subscription absent") {
                require(!decoder.stream(c.payload, receipt).trade_updates_subscribed,
                        "missing stream does not become success");
            }
            if (c.name == "incremental partial fill with subcent price") {
                auto payload = Json::parse(c.payload);
                payload["data"]["order"]["filled_avg_price"] = nullptr;
                rejects([&] { static_cast<void>(decoder.stream(payload.dump(), receipt)); });
                payload = Json::parse(c.payload);
                payload["data"]["order"]["legs"] = Json::array({Json::object()});
                rejects([&] { static_cast<void>(decoder.stream(payload.dump(), receipt)); });
                payload = Json::parse(c.payload);
                payload["data"]["order"]["filled_qty"] = "11";
                rejects([&] { static_cast<void>(decoder.stream(payload.dump(), receipt)); });
                payload = Json::parse(c.payload);
                payload["data"]["order"]["qty"] = true;
                rejects([&] { static_cast<void>(decoder.stream(payload.dump(), receipt)); });
                payload = Json::parse(c.payload);
                payload["data"]["position_qty"] = "-1";
                rejects([&] { static_cast<void>(decoder.stream(payload.dump(), receipt)); });
            }
        }
        rejects([&] { static_cast<void>(decoder.stream("[]", receipt)); });
        rejects([&] { static_cast<void>(decoder.stream("{", receipt)); });
        rejects([&] { static_cast<void>(decoder.stream(std::string(1024 * 1024 + 1, ' '), receipt)); });
        std::cout << "Paper endpoint, exact decimal, timestamp, message contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Paper contract tests failed: " << error.what() << '\n';
        return 1;
    }
}
