#pragma once
#include "market_calendar.hpp"
#include <utility>

namespace trading {
struct PaperMarketRequest {
    std::string url;
};

class PaperMarketRequests {
public:
    [[nodiscard]] PaperMarketRequest quote() const;
    [[nodiscard]] PaperMarketRequest calendar(const CalendarRange& range) const;
};

// Validated host selection and path, checked again before credentials are attached.
[[nodiscard]] std::pair<bool, std::string> validate_paper_market_request(const PaperMarketRequest& request);
} // namespace trading
