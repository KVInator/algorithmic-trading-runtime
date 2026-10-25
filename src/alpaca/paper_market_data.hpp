#pragma once
#include "alpaca_market_data_client.hpp"
#include "paper_transport.hpp"
#include "market_calendar.hpp"
#include "paper_market_request.hpp"

namespace trading {
struct PaperQuote {
    BrokerDecimal bid;
    BrokerDecimal ask;
    std::int64_t bid_lots;
    std::int64_t ask_lots;
    BrokerTimestamp source_at;
    std::string bid_exchange;
    std::string ask_exchange;
    std::string tape;
    std::vector<std::string> conditions;
};

class PaperQuoteDecoder {
public:
    [[nodiscard]] PaperQuote decode(std::string_view payload) const;
};

struct PaperQuoteAssessment {
    bool usable;
    std::vector<std::string> denials;
    std::int64_t source_age_ns;
    std::optional<std::int64_t> passive_buy_cents;
    std::optional<std::int64_t> marketable_buy_cents;
    std::optional<std::int64_t> marketable_sell_cents;
};

class PaperQuotePolicy {
public:
    // Re-evaluate with a fresh clock immediately before any future dispatch.
    [[nodiscard]] PaperQuoteAssessment assess(const PaperQuote& quote, const PaperReceipt& received,
                                              const PaperReceipt& now,
                                              const std::vector<CalendarSession>& sessions) const;
};

struct PaperMarketResult {
    bool native_probe{false};
    bool access_verified{false};
    std::size_t request_attempts{0};
    std::vector<std::string> checks;
    nlohmann::json cases = nlohmann::json::array();
    std::string detail;
};

class PaperMarketProbe {
public:
    using Read = std::function<MarketDataResponse(const PaperMarketRequest&)>;
    using Clock = std::function<PaperReceipt()>;
    PaperMarketProbe(Read read, Clock clock, PaperCapture& capture);
    [[nodiscard]] PaperMarketResult run() const;

private:
    Read read_;
    Clock clock_;
    PaperCapture& capture_;
};

class PaperMarketDiagnostic {
public:
    explicit PaperMarketDiagnostic(std::filesystem::path directory) : directory_{std::move(directory)} {}

    [[nodiscard]] PaperMarketResult run() const;

private:
    std::filesystem::path directory_;
};
} // namespace trading
