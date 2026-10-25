#include "paper_market_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace trading;
using Json = nlohmann::json;
constexpr std::string_view payload =
    R"({"symbol":"SPY","quote":{"bp":600.010000000000000001,"ap":600.010000000000000002,"bs":2,"as":3,"bx":"P","ax":"Q","t":"2026-10-26T14:30:00.123456789Z","c":["R"],"z":"B"}})";

void require(bool value, std::string_view message) {
    if (!value) {
        throw std::runtime_error(std::string{message});
    }
}

template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "unsupported market input must be rejected");
}

void calendar_contracts() {
    rejects([] { static_cast<void>(CalendarRange{"2026-10-26", "2026-10-30"}); });
    rejects([] { static_cast<void>(CalendarSession{"2026-10-26", "09:30", "16:00"}); });
    rejects([] { static_cast<void>(CalendarRange{"2025-12-31", "2026-01-01", CalendarRole::paper}); });
    rejects([] { static_cast<void>(CalendarRange{"2030-01-01", "2030-01-02", CalendarRole::paper}); });
    rejects([] { static_cast<void>(CalendarRange{"2026-02-29", "2026-03-01", CalendarRole::paper}); });
    rejects([] { static_cast<void>(CalendarRange{"2026-10-01", "2026-11-01", CalendarRole::paper}); });
    rejects([] { static_cast<void>(CalendarSession{"2026-10-25", "09:30", "16:00", CalendarRole::paper}); });
    rejects([] {
        static_cast<void>(CalendarSession{"2026-10-26", "09:30", "16:00", CalendarRole::paper}.bar_request());
    });
    for (const auto& [date, open] :
         std::vector<std::pair<std::string, std::string>>{{"2026-03-06", "2026-03-06T14:30:00Z"},
                                                          {"2026-03-09", "2026-03-09T13:30:00Z"},
                                                          {"2026-10-30", "2026-10-30T13:30:00Z"},
                                                          {"2026-11-02", "2026-11-02T14:30:00Z"}}) {
        require(CalendarSession{date, "09:30", "16:00", CalendarRole::paper}.open_utc() ==
                    parse_utc_seconds(open),
                "post-2007 DST boundary");
    }
    const CalendarRange range{"2026-11-23", "2026-11-30", CalendarRole::paper};
    const auto sessions = decode_market_calendar(
        R"([{"date":"2026-11-25","open":"09:30","close":"16:00"},{"date":"2026-11-27","open":"09:30","close":"13:00"}])",
        range);
    require(sessions.size() == 2 && !sessions[1].regular(),
            "provider holiday omission and shortened session retained");
    require(decode_market_calendar("[]", range).empty(), "current calendar may contain no trading sessions");
    rejects([&] {
        static_cast<void>(decode_market_calendar(
            R"([{"date":"2026-11-25","open":"09:30","close":"16:00"},{"date":"2026-11-25","open":"09:30","close":"16:00"}])",
            range));
    });
    rejects([&] {
        static_cast<void>(
            decode_market_calendar(R"([{"date":"2026-12-01","open":"09:30","close":"16:00"}])", range));
    });
}

void requests_and_decoding() {
    const PaperMarketRequests requests{};
    require(!validate_paper_market_request(requests.quote()).first, "quote is a fixed data-host GET");
    require(
        validate_paper_market_request(requests.calendar({"2026-10-26", "2026-11-25", CalendarRole::paper}))
            .first,
        "paper calendar host");
    rejects([&] { static_cast<void>(requests.calendar({"2022-01-03", "2022-01-31"})); });
    for (const auto url :
         {"http://data.alpaca.markets/v2/stocks/SPY/quotes/latest?feed=sip&currency=USD",
          "https://data.alpaca.markets/v2/stocks/SPY/quotes/latest?feed=iex&currency=USD",
          "https://data.alpaca.markets.evil/v2/stocks/SPY/quotes/latest?feed=sip&currency=USD",
          "https://paper-api.alpaca.markets/v2/orders",
          "https://api.alpaca.markets/v2/calendar?start=2026-10-26&end=2026-10-30&date_type=TRADING",
          "https://paper-api.alpaca.markets/v2/"
          "calendar?start=2026-10-26&end=2026-10-30&date_type=TRADING&x=1"}) {
        rejects([&] { static_cast<void>(validate_paper_market_request({url})); });
    }
    const auto q = PaperQuoteDecoder{}.decode(payload);
    require(q.bid.text() == "600.010000000000000001" && q.ask.text() == "600.010000000000000002" &&
                q.bid < q.ask,
            "numeric price lexemes survive a binary-double collision");
    require(q.source_at.value() == BrokerTimestamp{"2026-10-26T10:30:00.123456789-04:00"}.value(),
            "source nanoseconds/offset retained");
    auto json = Json::parse(payload);
    for (const auto field : {"bp", "ap", "bs", "as", "t", "bx", "ax", "c", "z"}) {
        auto bad = json;
        bad["quote"].erase(field);
        rejects([&] { static_cast<void>(PaperQuoteDecoder{}.decode(bad.dump())); });
    }
    json["quote"]["bp"] = "600.01";
    rejects([&] { static_cast<void>(PaperQuoteDecoder{}.decode(json.dump())); });
    json = Json::parse(payload);
    json["quote"]["bs"] = 1.5;
    rejects([&] { static_cast<void>(PaperQuoteDecoder{}.decode(json.dump())); });
    json = Json::parse(payload);
    json["symbol"] = "AAPL";
    rejects([&] { static_cast<void>(PaperQuoteDecoder{}.decode(json.dump())); });
    rejects([] { static_cast<void>(PaperQuoteDecoder{}.decode("{\"symbol\":\"SPY\",\"symbol\":\"SPY\"}")); });
    rejects([] { static_cast<void>(PaperQuoteDecoder{}.decode(std::string(65537, ' '))); });
    const auto exact =
        parse_json_number_text(R"({"s":"12 \" 7","n":1.000000000000000001,"a":[true,null,-2]})");
    require(exact.at("n").get<std::string>() == "1.000000000000000001" &&
                exact.at("a").at(2).get<std::string>() == "-2",
            "lexical scan respects escaped strings and nonnumeric values");
}

void quote_gates() {
    auto quote = PaperQuoteDecoder{}.decode(payload);
    const std::vector<CalendarSession> sessions{{"2026-10-26", "09:30", "16:00", CalendarRole::paper}};
    const PaperReceipt received{"2026-10-26T14:30:01.123456789Z", 1000};
    const auto assess = [&](const PaperQuote& q, const PaperReceipt& at) {
        return PaperQuotePolicy{}.assess(q, received, at, sessions);
    };
    const auto accepted = assess(quote, received);
    require(accepted.usable && accepted.source_age_ns == 1000000000 && accepted.passive_buy_cents == 60001 &&
                accepted.marketable_buy_cents == 60002 && accepted.marketable_sell_cents == 60001,
            "exact directed cent projections");
    require(assess(quote, {"2026-10-26T14:30:02.123456789Z", 2000}).usable, "exact two-second age allowed");
    require(!assess(quote, {"2026-10-26T14:30:02.123456790Z", 2000}).usable,
            "one ns beyond age bound denied");
    quote.source_at = BrokerTimestamp{"2026-10-26T14:30:01.123456790Z"};
    require(!assess(quote, received).usable, "one ns in future denied");
    quote = PaperQuoteDecoder{}.decode(payload);
    require(!assess(quote, {received.utc, 999}).usable, "monotonic regression denied");
    require(!assess(quote, {"2026-10-26T14:30:01.123456788Z", 1001}).usable, "UTC regression denied");
    require(!assess(quote, {received.utc, 1500}).usable, "UTC/monotonic disagreement denied");
    require(!assess(quote, {received.utc, 3001}).usable,
            "held receipt cannot stay fresh on a frozen UTC clock");
    for (int defect = 0; defect < 5; ++defect) {
        auto q = quote;
        if (defect == 0) {
            q.bid = q.ask;
        }
        if (defect == 1) {
            q.bid = BrokerDecimal{"601"};
        }
        if (defect == 2) {
            q.bid_lots = 0;
        }
        if (defect == 3) {
            q.ask = BrokerDecimal{"0.99"};
        }
        if (defect == 4) {
            q.bid = BrokerDecimal{"9223372036854775808"};
            q.ask = BrokerDecimal{"9223372036854775809"};
        }
        const auto a = assess(q, received);
        require(!a.usable && !a.passive_buy_cents && !a.marketable_buy_cents && !a.marketable_sell_cents,
                "denial never returns routable limits");
    }
    const std::vector<CalendarSession> shortened{{"2026-10-26", "09:30", "13:00", CalendarRole::paper}};
    require(!PaperQuotePolicy{}.assess(quote, received, received, shortened).usable,
            "shortened sessions excluded");
    require(!PaperQuotePolicy{}.assess(quote, received, received, {}).usable,
            "missing calendar session denied");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "market tests require an output root");
        calendar_contracts();
        requests_and_decoding();
        quote_gates();
        const auto directory = create_unique_directory(argv[1], "paper-market");
        const auto result = PaperMarketDiagnostic{directory}.run();
        PaperMarketReport{result}.write(directory);
        require(result.checks.size() == 6 && result.cases.size() == 4, "constructed access cases");
        const auto summary = Json::parse(read_text_file(directory / "summary.json"));
        require(!summary.at("service_access_observed").get<bool>() &&
                    !summary.at("session_enrollment_created").get<bool>() &&
                    summary.at("network_requests_sent").get<int>() == 0,
                "constructed report does not claim service access/enrollment");
        for (const auto& entry : summary.at("artifacts")) {
            require(sha256_file(directory / entry.at("path").get<std::string>()) ==
                        entry.at("sha256").get<std::string>(),
                    "market artifact hashes");
        }
        std::cout << "Paper market contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
