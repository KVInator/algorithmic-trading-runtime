#include "alpaca_bar_page.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;

// Synthetic provider-shaped data, not historical observations.
Json sample_page() {
    return Json::parse(R"({
        "bars":{"SPY":[
            {"t":"2023-01-03T14:30:00Z","o":100,"h":102,"l":99,"c":101,"v":1000,"n":10,"vw":100.5},
            {"t":"2023-01-03T14:31:00Z","o":101,"h":103,"l":100,"c":102,"v":2000.5,"n":20,"vw":101.8}
        ]},
        "next_page_token":"OPAQUE+/TOKEN=="
    })");
}

void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string{description});
    }
}

template <typename Operation>
void expect_invalid(Operation operation, std::string_view description) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string{description});
}

void reject(const Json& value, std::string_view description) {
    expect_invalid([&] {
        static_cast<void>(trading::decode_alpaca_bar_page(value.dump(), "SPY"));
    }, description);
}

void test_valid_pages() {
    auto document = sample_page();
    const auto page = trading::decode_alpaca_bar_page(document.dump(), "SPY");
    require(page.bars.size() == 2, "both records must be decoded");
    require(page.bars.front().symbol() == "SPY", "the requested symbol must be retained");
    const auto& first = page.bars.front().prices();
    require(first.open == 100 && first.high == 102 && first.low == 99 && first.close == 101,
            "provider fields must map to the correct OHLC members");
    require(page.bars.back().volume() == 2000.5, "fractional volume must not be truncated");
    require(page.next_page_token && *page.next_page_token == "OPAQUE+/TOKEN==",
            "pagination tokens must be preserved exactly");
    require(page.bars.back().start() - page.bars.front().start() == std::chrono::minutes{1},
            "UTC timestamps must preserve the one-minute interval");

    document["next_page_token"] = nullptr;
    require(!trading::decode_alpaca_bar_page(document.dump(), "SPY").next_page_token,
            "null must represent the end of pagination");

    document["bars"]["SPY"][1]["t"] = "2023-01-03T14:33:00.000000000Z";
    const auto gapped = trading::decode_alpaca_bar_page(document.dump(), "SPY");
    require(gapped.bars.size() == 2 &&
                gapped.bars.back().start() - gapped.bars.front().start() == std::chrono::minutes{3},
            "gaps must remain visible without fabricated bars");

    document["bars"]["SPY"] = Json::array();
    require(trading::decode_alpaca_bar_page(document.dump(), "SPY").bars.empty(),
            "an empty symbol array is a valid empty page");
    document["bars"] = Json::object();
    require(trading::decode_alpaca_bar_page(document.dump(), "SPY").bars.empty(),
            "an empty bars object is a valid empty page");
}

void test_schema_failures() {
    auto value = sample_page();
    value.erase("bars");
    reject(value, "missing bars must be rejected");
    value = sample_page();
    value.erase("next_page_token");
    reject(value, "missing pagination status must be rejected");
    value = sample_page();
    value["next_page_token"] = "";
    reject(value, "an empty token must not be treated as null");
    value["next_page_token"] = false;
    reject(value, "a boolean token must be rejected");
    value = sample_page();
    value["bars"]["QQQ"] = value["bars"]["SPY"];
    reject(value, "unexpected additional symbols must be rejected");
    value["bars"].erase("SPY");
    reject(value, "a response for the wrong symbol must be rejected");
    value = sample_page();
    value["bars"]["SPY"] = nullptr;
    reject(value, "records must be an array");
    value = sample_page();
    value["bars"]["SPY"][0].erase("h");
    reject(value, "missing numeric fields must be rejected");
    value = sample_page();
    value["bars"]["SPY"][0]["c"] = true;
    reject(value, "booleans must not be coerced to prices");
    value["bars"]["SPY"][0]["c"] = "101";
    reject(value, "numeric strings must not be coerced to prices");
    value = sample_page();
    value["bars"]["SPY"][0]["v"] = nullptr;
    reject(value, "null volume must not become zero");
    value = sample_page();
    value["bars"]["SPY"][0]["c"] = 104;
    reject(value, "domain validation must reject a close outside the high/low range");
    value = sample_page();
    value["bars"]["SPY"][0]["v"] = -1;
    reject(value, "domain validation must reject negative volume");
}

void test_time_and_ordering() {
    const auto leap = trading::parse_utc_seconds("2024-02-29T23:59:00.000Z");
    require(trading::format_utc_seconds(leap) == "2024-02-29T23:59:00Z",
            "valid leap-day timestamps must round-trip at whole-second precision");
    for (const std::string_view timestamp : {
             "2023-02-29T14:30:00Z", "2023-04-31T14:30:00Z", "0000-01-01T00:00:00Z",
             "2023-01-03T24:00:00Z", "2023-01-03T14:60:00Z", "2023-01-03T14:30:60Z",
             "2023-01-03T14:30:00+00:00", "2023-01-03T14:30:00.001Z", "bad timestamp"}) {
        expect_invalid([&] { static_cast<void>(trading::parse_utc_seconds(timestamp)); },
                       "invalid or unsupported UTC timestamps must be rejected");
    }
    auto value = sample_page();
    value["bars"]["SPY"][0]["t"] = "2023-01-03T14:30:01Z";
    reject(value, "minute alignment must be enforced by the domain object");
    value = sample_page();
    value["bars"]["SPY"][1]["t"] = "2023-01-03T14:30:00Z";
    reject(value, "duplicate timestamps must be rejected");
    value["bars"]["SPY"][1]["t"] = "2023-01-03T14:29:00Z";
    reject(value, "descending timestamps must be rejected without automatic sorting");
}

void test_json_failures() {
    for (const std::string_view payload : {
             "not JSON", "[]", R"({"message":"API error"})",
             R"({"bars":{},"bars":{},"next_page_token":null})",
             R"({"bars":{"SPY":[{"t":"2023-01-03T14:30:00Z","o":100,"o":101,"h":102,"l":99,"c":101,"v":1}]},"next_page_token":null})",
             R"({"bars":{"SPY":[{"t":"2023-01-03T14:30:00Z","o":100,"h":102,"l":99,"c":1e400,"v":1}]},"next_page_token":null})"}) {
        expect_invalid([&] { static_cast<void>(trading::decode_alpaca_bar_page(payload, "SPY")); },
                       "malformed JSON, duplicate keys and numeric overflow must be rejected");
    }
}

} // namespace

int main() {
    try {
        test_valid_pages();
        test_schema_failures();
        test_time_and_ordering();
        test_json_failures();
        std::cout << "Alpaca bar-page contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Alpaca bar-page contract failure: " << error.what() << '\n';
        return 1;
    }
}
