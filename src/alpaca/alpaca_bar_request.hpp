#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace trading {

// One symbol and a half-open UTC minute grid, bounded to at most 24 hours.
// The request explicitly selects 1Min, SIP, raw, USD and no symbol mapping.
class HistoricalBarRequest {
public:
    HistoricalBarRequest(std::string symbol, std::chrono::sys_seconds start,
                         std::chrono::sys_seconds end_exclusive, std::size_t page_size = 100);

    [[nodiscard]] const std::string& symbol() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds start() const noexcept;
    [[nodiscard]] std::chrono::sys_seconds end_exclusive() const noexcept;
    [[nodiscard]] std::size_t page_size() const noexcept;
    [[nodiscard]] std::size_t expected_minutes() const noexcept;
    [[nodiscard]] std::string path(const std::optional<std::string>& page_token = std::nullopt) const;

private:
    std::string symbol_;
    std::chrono::sys_seconds start_;
    std::chrono::sys_seconds end_exclusive_;
    std::size_t page_size_;
};

} // namespace trading
