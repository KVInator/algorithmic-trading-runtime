#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace trading {
class CostQuoteReport {
  public:
    void write(const std::filesystem::path &directory, const nlohmann::json &summary) const;
    void render_existing(const std::filesystem::path &directory,
                         const std::filesystem::path &saved_summary) const;
};
} // namespace trading
