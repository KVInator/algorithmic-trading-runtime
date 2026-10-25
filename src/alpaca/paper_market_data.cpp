#include "paper_market_data.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;

std::string text(const Json& item, const char* field) {
    if (!item.is_object() || !item.contains(field) || !item.at(field).is_string() ||
        item.at(field).get_ref<const std::string&>().empty() ||
        item.at(field).get_ref<const std::string&>().size() > 80) {
        throw std::invalid_argument(std::string{"quote requires bounded text: "} + field);
    }
    return item.at(field).get<std::string>();
}

std::int64_t cents(const BrokerDecimal& price, bool up) {
    if (price < BrokerDecimal{"1"}) {
        throw std::invalid_argument("quote price is below one USD");
    }
    const auto& value = price.text();
    const auto point = value.find('.');
    const auto whole = BrokerDecimal{std::string_view{value}.substr(0, point)}.scaled_integer(0);
    if (whole > (std::numeric_limits<std::int64_t>::max() - 100) / 100) {
        throw std::invalid_argument("quote limit exceeds supported integer cents");
    }
    std::int64_t result = whole * 100;
    if (point != value.npos) {
        const auto fraction = std::string_view{value}.substr(point + 1);
        result += static_cast<int>(fraction[0] - '0') * 10;
        if (fraction.size() > 1) {
            result += fraction[1] - '0';
        }
        if (up && fraction.size() > 2) {
            ++result;
        } // Canonical decimal has a nonzero final digit.
    }
    return result;
}

Json receipt(const PaperReceipt& r) { return {{"utc", r.utc}, {"monotonic_ms", r.monotonic_ms}}; }

Json quote_evidence(const PaperQuote& q, const PaperQuoteAssessment& a, const PaperReceipt& received,
                    const PaperReceipt& evaluated) {
    const auto optional = [](const auto& v) { return v ? Json(*v) : Json(nullptr); };
    return {{"case", "latest_sip_quote"},
            {"symbol", "SPY"},
            {"requested_feed", "sip"},
            {"currency", "USD"},
            {"bid", q.bid.text()},
            {"ask", q.ask.text()},
            {"bid_size_round_lots", q.bid_lots},
            {"ask_size_round_lots", q.ask_lots},
            {"bid_exchange", q.bid_exchange},
            {"ask_exchange", q.ask_exchange},
            {"tape", q.tape},
            {"conditions", q.conditions},
            {"source_at", q.source_at.text()},
            {"received", receipt(received)},
            {"evaluated", receipt(evaluated)},
            {"source_age_ns", a.source_age_ns},
            {"quote_usable", a.usable},
            {"denials", a.denials},
            {"passive_buy_cents", optional(a.passive_buy_cents)},
            {"marketable_buy_cents", optional(a.marketable_buy_cents)},
            {"marketable_sell_cents", optional(a.marketable_sell_cents)}};
}
} // namespace

PaperQuote PaperQuoteDecoder::decode(std::string_view payload) const {
    if (payload.empty() || payload.size() > maximum_market_data_page_bytes) {
        throw std::invalid_argument("quote payload exceeds 64 KiB or is empty");
    }
    const auto original = parse_json_document(payload);
    if (text(original, "symbol") != "SPY" || !original.contains("quote") ||
        !original.at("quote").is_object()) {
        throw std::invalid_argument("latest quote requires a SPY quote object");
    }
    const auto& types = original.at("quote");
    for (const auto field : {"bp", "ap", "bs", "as"}) {
        if (!types.contains(field) || !types.at(field).is_number()) {
            throw std::invalid_argument("quote price/size must be a JSON number");
        }
    }
    if (!types.at("bs").is_number_integer() || !types.at("as").is_number_integer()) {
        throw std::invalid_argument("quote sizes require integer round lots");
    }
    const auto exact = parse_json_number_text(payload);
    const auto& q = exact.at("quote");
    if (!q.contains("c") || !q.at("c").is_array() || q.at("c").size() > 32) {
        throw std::invalid_argument("quote conditions require a bounded array");
    }
    std::vector<std::string> conditions;
    for (const auto& condition : q.at("c")) {
        if (!condition.is_string() || condition.get_ref<const std::string&>().empty() ||
            condition.get_ref<const std::string&>().size() > 16) {
            throw std::invalid_argument("invalid quote condition code");
        }
        conditions.push_back(condition.get<std::string>());
    }
    return {BrokerDecimal{text(q, "bp")},
            BrokerDecimal{text(q, "ap")},
            BrokerDecimal{text(q, "bs")}.scaled_integer(0),
            BrokerDecimal{text(q, "as")}.scaled_integer(0),
            BrokerTimestamp{text(q, "t")},
            text(q, "bx"),
            text(q, "ax"),
            text(q, "z"),
            std::move(conditions)};
}

PaperQuoteAssessment PaperQuotePolicy::assess(const PaperQuote& q, const PaperReceipt& received,
                                              const PaperReceipt& now,
                                              const std::vector<CalendarSession>& sessions) const {
    const auto at = BrokerTimestamp{now.utc}.value(), arrival = BrokerTimestamp{received.utc}.value();
    const auto age = (at - q.source_at.value()).count();
    PaperQuoteAssessment result{false, {}, age, {}, {}, {}};
    auto& denied = result.denials;
    if (received.monotonic_ms < 0 || now.monotonic_ms < received.monotonic_ms || at < arrival) {
        denied.emplace_back("receipt_clock_regression");
    } else {
        const auto elapsed = now.monotonic_ms - received.monotonic_ms;
        if (elapsed > 2000) {
            denied.emplace_back("receipt_too_old");
        } else {
            const auto difference = (at - arrival).count() - elapsed * 1000000;
            if (difference < -250000000 || difference > 250000000) {
                denied.emplace_back("receipt_clock_disagreement");
            }
        }
    }
    if (age < 0) {
        denied.emplace_back("future_source_timestamp");
    } else if (age > 2000000000) {
        denied.emplace_back("source_age_over_two_seconds");
    }
    if (q.bid < BrokerDecimal{"1"} || q.ask < BrokerDecimal{"1"}) {
        denied.emplace_back("price_below_one_usd");
    }
    if (q.bid >= q.ask) {
        denied.emplace_back("crossed_or_locked");
    }
    if (q.bid_lots <= 0 || q.ask_lots <= 0) {
        denied.emplace_back("nonpositive_displayed_size");
    }
    const auto session = std::find_if(sessions.begin(), sessions.end(), [&](const auto& s) {
        return at >= s.open_utc() && at < s.close_utc();
    });
    if (session == sessions.end()) {
        denied.emplace_back("outside_calendar_session");
    } else if (!session->regular()) {
        denied.emplace_back("shortened_session_excluded");
    } else if (q.source_at.value() < session->open_utc() || q.source_at.value() >= session->close_utc()) {
        denied.emplace_back("quote_outside_current_session");
    }
    if (denied.empty()) {
        try {
            result.passive_buy_cents = cents(q.bid, false);
            result.marketable_buy_cents = cents(q.ask, true);
            result.marketable_sell_cents = cents(q.bid, false);
            result.usable = true;
        } catch (const std::invalid_argument&) {
            result.passive_buy_cents.reset();
            result.marketable_buy_cents.reset();
            result.marketable_sell_cents.reset();
            denied.emplace_back("limit_cent_range_unsupported");
        }
    }
    return result;
}

PaperMarketProbe::PaperMarketProbe(Read read, Clock clock, PaperCapture& capture)
    : read_{std::move(read)}, clock_{std::move(clock)}, capture_{capture} {
    if (!read_ || !clock_) {
        throw std::invalid_argument("paper market probe requires reader and clocks");
    }
}

PaperMarketResult PaperMarketProbe::run() const {
    PaperMarketResult result{};
    const auto exchange = [&](const PaperMarketRequest& request) {
        static_cast<void>(validate_paper_market_request(request));
        const auto started = clock_();
        capture_.record("market_request", "GET " + request.url, "", started);
        ++result.request_attempts;
        auto response = read_(request);
        const auto received = clock_();
        capture_.record("market_response", "GET " + request.url, response.body, received, response.status, "",
                        response.retry_after.value_or(""));
        if (received.monotonic_ms < started.monotonic_ms ||
            BrokerTimestamp{received.utc}.value() < BrokerTimestamp{started.utc}.value()) {
            throw std::runtime_error("read-only request clock regression");
        }
        if (response.status != 200) {
            throw std::runtime_error("read-only GET returned HTTP " + std::to_string(response.status));
        }
        return std::pair{std::move(response), received};
    };
    try {
        const auto started = clock_();
        const auto first = std::chrono::floor<std::chrono::days>(BrokerTimestamp{started.utc}.value());
        const auto last =
            std::min(first + std::chrono::days{30}, std::chrono::sys_days{std::chrono::year{2029} / 12 / 31});
        const CalendarRange range{format_utc_seconds(first).substr(0, 10),
                                  format_utc_seconds(last).substr(0, 10), CalendarRole::paper};
        const auto [calendar, calendar_received] = exchange(PaperMarketRequests{}.calendar(range));
        const auto sessions = decode_market_calendar(calendar.body, range);
        Json rows = Json::array(), candidates = Json::array();
        for (const auto& s : sessions) {
            Json row{{"date", s.date()},
                     {"open_utc", format_utc_seconds(s.open_utc())},
                     {"close_utc", format_utc_seconds(s.close_utc())},
                     {"regular", s.regular()}};
            rows.push_back(row);
            if (s.regular() && s.open_utc() > BrokerTimestamp{calendar_received.utc}.value() &&
                candidates.size() < 5) {
                candidates.push_back(row);
            }
        }
        result.cases.push_back({{"case", "current_calendar"},
                                {"first", range.first()},
                                {"last", range.last()},
                                {"received", receipt(calendar_received)},
                                {"sessions", rows},
                                {"candidate_sessions", candidates},
                                {"five_candidates_available", candidates.size() == 5},
                                {"enrolled", false}});
        result.checks.emplace_back(
            "current calendar decoded with explicit paper role; historical boundary retained");
        const auto [response, received] = exchange(PaperMarketRequests{}.quote());
        const auto quote = PaperQuoteDecoder{}.decode(response.body);
        const auto evaluated = clock_();
        const auto assessment = PaperQuotePolicy{}.assess(quote, received, evaluated, sessions);
        result.cases.push_back(quote_evidence(quote, assessment, received, evaluated));
        result.checks.emplace_back(
            "explicit SIP/USD SPY request decoded without floating-point price arithmetic");
        result.checks.emplace_back("quote freshness, calendar and cent-rounding gates evaluated");
        result.access_verified = true;
        result.detail = assessment.usable
                            ? "Read-only access verified; quote usable at the recorded evaluation only. No "
                              "enrollment or orders."
                            : "Read-only access verified; quote is not usable. Retained denials explain "
                              "off-hours, stale or invalid market conditions. No enrollment or orders.";
        capture_.record("connection", "paper_market_checks_completed", "", clock_());
    } catch (const std::exception& e) {
        result.detail = e.what();
        result.access_verified = false;
        capture_.record("connection", "paper_market_probe_stopped", "", clock_());
    }
    return result;
}
} // namespace trading
