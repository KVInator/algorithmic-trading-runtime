#pragma once

#include "alpaca_credentials.hpp"
#include "alpaca_bar_request.hpp"
#include "market_calendar.hpp"
#include "alpaca_corporate_action_request.hpp"
#include "historical_tick_request.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <optional>

namespace trading {

inline constexpr std::size_t maximum_market_data_page_bytes = 64U * 1024U;

struct MarketDataResponse {
    std::uint32_t status;
    std::string body;
    std::optional<std::string> retry_after{};
};

// Read-only Windows transport for historical bars, action probes and the calendar.
class AlpacaMarketDataClient {
  public:
    explicit AlpacaMarketDataClient(AlpacaCredentials credentials);

    [[nodiscard]] MarketDataResponse
    get_historical_bars(const HistoricalBarRequest &request,
                        const std::optional<std::string> &page_token = std::nullopt) const;
    [[nodiscard]] MarketDataResponse get_market_calendar(const CalendarRange &range) const;
    [[nodiscard]] MarketDataResponse
    get_historical_ticks(const HistoricalTickRequest &request,
                         const std::optional<std::string> &page_token = std::nullopt) const;
    [[nodiscard]] MarketDataResponse get_corporate_action_probe(int year) const;
    [[nodiscard]] MarketDataResponse
    get_corporate_actions(const CorporateActionRequest &request,
                          const std::optional<std::string> &token = std::nullopt) const;

  private:
    [[nodiscard]] MarketDataResponse get_page(bool calendar, const std::string &path) const;
    AlpacaCredentials credentials_;
};

} // namespace trading
