#pragma once

#include "alpaca_market_data_client.hpp"
#include "minute_bar.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace trading {

struct SessionAudit {
    std::size_t expected_minutes;
    std::size_t observed_bars;
    std::vector<std::chrono::sys_seconds> missing_minutes;
    double total_volume_shares;

    [[nodiscard]] bool complete() const noexcept { return missing_minutes.empty(); }
};

// Rejects symbol, ordering and boundary violations; reports missing grid points.
[[nodiscard]] SessionAudit audit_session(const HistoricalBarRequest& request,
                                         std::span<const MinuteBar> bars);

struct DownloadedPage {
    std::string request_path;
    std::string raw_json;
    std::size_t bar_count;
};

struct SessionDownload {
    HistoricalBarRequest request;
    std::vector<MinuteBar> bars;
    std::vector<DownloadedPage> pages;
    SessionAudit audit;
    std::chrono::sys_seconds started_utc;
    std::chrono::sys_seconds finished_utc;
};

// The live WinHTTP client and offline fixtures supply the same page contract.
using BarPageFetcher = std::function<MarketDataResponse(
    const HistoricalBarRequest&, const std::optional<std::string>&)>;

class SessionDownloader {
public:
    explicit SessionDownloader(HistoricalBarRequest request, std::size_t maximum_pages = 16);
    [[nodiscard]] SessionDownload download(const BarPageFetcher& fetch_page) const;

private:
    HistoricalBarRequest request_;
    std::size_t maximum_pages_;
};

} // namespace trading
