#include "session_download.hpp"
#include "alpaca_bar_page.hpp"

#include <cmath>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace trading {

SessionAudit audit_session(const HistoricalBarRequest& request, std::span<const MinuteBar> bars) {
    SessionAudit audit{request.expected_minutes(), bars.size(), {}, 0.0};
    auto next_expected = request.start();
    for (const auto& bar : bars) {
        if (bar.symbol() != request.symbol()) {
            throw std::invalid_argument("session contains a bar for a different symbol");
        }
        if (bar.start() < request.start() || bar.start() >= request.end_exclusive()) {
            throw std::invalid_argument("bar timestamp falls outside the requested session");
        }
        if (bar.start() < next_expected) {
            throw std::invalid_argument("session timestamps are duplicated or out of order");
        }
        while (next_expected < bar.start()) {
            audit.missing_minutes.push_back(next_expected);
            next_expected += std::chrono::minutes{1};
        }
        next_expected += std::chrono::minutes{1};
        audit.total_volume_shares += bar.volume();
        if (!std::isfinite(audit.total_volume_shares)) {
            throw std::overflow_error("session volume exceeds the numeric range");
        }
    }
    while (next_expected < request.end_exclusive()) {
        audit.missing_minutes.push_back(next_expected);
        next_expected += std::chrono::minutes{1};
    }
    return audit;
}

SessionDownloader::SessionDownloader(HistoricalBarRequest request, std::size_t maximum_pages)
    : request_{std::move(request)}, maximum_pages_{maximum_pages} {
    if (maximum_pages_ == 0 || maximum_pages_ > 64) {
        throw std::invalid_argument("session download requires a page bound between 1 and 64");
    }
}

SessionDownload SessionDownloader::download(const BarPageFetcher& fetch_page) const {
    if (!fetch_page) {
        throw std::invalid_argument("session download requires a page fetcher");
    }
    const auto started = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::vector<MinuteBar> bars;
    std::vector<DownloadedPage> pages;
    std::unordered_set<std::string> seen_tokens;
    std::optional<std::string> token;
    while (true) {
        if (pages.size() >= maximum_pages_) {
            throw std::runtime_error("session download exceeded its page bound before pagination ended");
        }
        const auto path = request_.path(token);
        auto response = fetch_page(request_, token);
        if (response.status != 200) {
            throw std::runtime_error("historical bars request failed with HTTP " +
                                     std::to_string(response.status) + " on page " +
                                     std::to_string(pages.size() + 1));
        }
        if (response.body.size() > maximum_market_data_page_bytes) {
            throw std::runtime_error("market-data response page exceeds the 64 KiB limit");
        }
        auto page = decode_alpaca_bar_page(response.body, request_.symbol());
        if (page.bars.size() > request_.page_size() ||
            page.bars.size() > request_.expected_minutes() - bars.size()) {
            throw std::invalid_argument("response exceeds the requested page or session row bound");
        }
        if (!bars.empty() && !page.bars.empty() && page.bars.front().start() <= bars.back().start()) {
            throw std::invalid_argument("timestamps overlap or descend across response pages");
        }
        for (const auto& bar : page.bars) {
            if (bar.start() < request_.start() || bar.start() >= request_.end_exclusive()) {
                throw std::invalid_argument("response contains a bar outside the requested session");
            }
        }
        if (page.next_page_token) {
            static_cast<void>(request_.path(page.next_page_token));
            if (!seen_tokens.insert(*page.next_page_token).second) {
                throw std::invalid_argument("pagination token repeated before completion");
            }
        }
        pages.push_back({path, std::move(response.body), page.bars.size()});
        bars.insert(bars.end(), std::make_move_iterator(page.bars.begin()),
                    std::make_move_iterator(page.bars.end()));
        token = std::move(page.next_page_token);
        if (!token) {
            break;
        }
    }
    auto audit = audit_session(request_, bars);
    const auto finished = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return {request_, std::move(bars), std::move(pages), std::move(audit), started, finished};
}

} // namespace trading
