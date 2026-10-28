#pragma once
#include "cost_quote_sample.hpp"

namespace trading {
// Read-only replay of the retained quote sample. No alternative trading policy
// is selected; diagnostic projections contain no prices or order instructions.
class CostQuoteDiagnostics {
  public:
    explicit CostQuoteDiagnostics(std::filesystem::path panel_directory);
    [[nodiscard]] nlohmann::json inspect(const std::filesystem::path &saved_summary,
                                         const std::function<void(const std::string &)> &progress = {}) const;

  private:
    std::filesystem::path panel_directory_;
};
} // namespace trading
