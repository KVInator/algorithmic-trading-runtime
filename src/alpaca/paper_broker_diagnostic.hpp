#pragma once
#include "paper_transport.hpp"

namespace trading {
struct PaperBrokerResult {
    std::vector<std::string> checks;
    nlohmann::json cases = nlohmann::json::array();
    std::string detail;
};

class PaperBrokerDiagnostic {
  public:
    explicit PaperBrokerDiagnostic(std::filesystem::path directory) : directory_{std::move(directory)} {}
    [[nodiscard]] PaperBrokerResult run() const;

  private:
    std::filesystem::path directory_;
};
} // namespace trading
