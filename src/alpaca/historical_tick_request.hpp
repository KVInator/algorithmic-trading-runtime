#pragma once
#include <chrono>
#include <optional>
#include <string>

namespace trading {
enum class HistoricalTickKind { quotes, trades };

// One old SIP symbol/window; inclusive endpoints and a bounded first-page sample.
class HistoricalTickRequest {
public:
    HistoricalTickRequest(HistoricalTickKind kind, std::string symbol,
                          std::chrono::sys_seconds start, std::chrono::sys_seconds end);
    [[nodiscard]] HistoricalTickKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& symbol() const noexcept { return symbol_; }
    [[nodiscard]] std::chrono::sys_seconds start() const noexcept { return start_; }
    [[nodiscard]] std::chrono::sys_seconds end() const noexcept { return end_; }
    [[nodiscard]] std::string path(const std::optional<std::string>& token = std::nullopt) const;
private:
    HistoricalTickKind kind_;
    std::string symbol_;
    std::chrono::sys_seconds start_, end_;
};
} // namespace trading
