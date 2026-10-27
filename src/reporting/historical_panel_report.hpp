#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

namespace trading {
class HistoricalPanelReport {
public:
    // Counts, masks and hashes only. Raw prices and reserved inputs stay in the dataset.
    [[nodiscard]] std::filesystem::path write(const nlohmann::json& summary,
        const std::filesystem::path& run_root) const;
    // No prices; cash discrepancy amounts are limited to pre-2024 actions.
    [[nodiscard]] std::filesystem::path write_readiness(const nlohmann::json& assessment,
        const std::filesystem::path& run_root) const;
};
} // namespace trading
