#include "paper_market_request.hpp"
#include <stdexcept>

namespace trading {
PaperMarketRequest PaperMarketRequests::quote() const {
    return {"https://data.alpaca.markets/v2/stocks/SPY/quotes/latest?feed=sip&currency=USD"};
}

PaperMarketRequest PaperMarketRequests::calendar(const CalendarRange& range) const {
    if (range.role() != CalendarRole::paper) {
        throw std::invalid_argument("current paper calendar role required");
    }
    return {"https://paper-api.alpaca.markets" + range.path()};
}

std::pair<bool, std::string> validate_paper_market_request(const PaperMarketRequest& request) {
    if (request.url == PaperMarketRequests{}.quote().url) {
        return {false, request.url.substr(std::string_view{"https://data.alpaca.markets"}.size())};
    }
    constexpr std::string_view origin = "https://paper-api.alpaca.markets";
    const std::string prefix = std::string{origin} + "/v2/calendar?start=";
    if (!request.url.starts_with(prefix)) {
        throw std::invalid_argument("unsupported read-only paper market URL");
    }
    const auto remainder = std::string_view{request.url}.substr(prefix.size());
    if (remainder.size() != 43 || remainder.substr(10, 5) != "&end=" ||
        remainder.substr(25) != "&date_type=TRADING") {
        throw std::invalid_argument("noncanonical paper calendar query");
    }
    const CalendarRange range{std::string{remainder.substr(0, 10)}, std::string{remainder.substr(15, 10)},
                              CalendarRole::paper};
    if (PaperMarketRequests{}.calendar(range).url != request.url) {
        throw std::invalid_argument("noncanonical paper calendar URL");
    }
    return {true, range.path()};
}
} // namespace trading
