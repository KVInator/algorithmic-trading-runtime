#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

namespace trading {
class HistoricalPanelReport {
public:
    // Counts, masks and hashes only. Raw prices and reserved inputs stay in the dataset.
    [[nodiscard]] std::filesystem::path write(const nlohmann::json& summary,
        const std::filesystem::path& run_root) const;
};
} // namespace trading
