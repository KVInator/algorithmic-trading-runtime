#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace trading {
class CostQuoteReport {
  public:
    void write(const std::filesystem::path &directory, const nlohmann::json &summary) const;
};
} // namespace trading
