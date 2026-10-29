#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

namespace trading {
// One-sided hypothetical crossing relative to the contemporaneous midpoint.
// This descriptive comparison neither replays strategies nor estimates fills.
class QuoteReferenceStudy {
  public:
    [[nodiscard]] nlohmann::json evaluate(const nlohmann::json &sample) const;
    void write(const std::filesystem::path &directory, const nlohmann::json &sample,
               const nlohmann::json &authentication) const;
};
} // namespace trading
