#pragma once
#include "paper_transport_result.hpp"

namespace trading {
class PaperBrokerDiagnostic {
  public:
    explicit PaperBrokerDiagnostic(std::filesystem::path directory) : directory_{std::move(directory)} {}
    [[nodiscard]] PaperTransportResult run() const;

  private:
    std::filesystem::path directory_;
};
} // namespace trading
