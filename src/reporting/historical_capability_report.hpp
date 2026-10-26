#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

namespace trading {
class HistoricalCapabilityReport {
public:
    void write(const std::filesystem::path& directory, nlohmann::json summary) const;
};
} // namespace trading
