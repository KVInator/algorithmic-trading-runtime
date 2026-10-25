#pragma once
#include "paper_transport_result.hpp"

namespace trading {
class PaperTransportDiagnostic {
  public:
    explicit PaperTransportDiagnostic(std::filesystem::path directory) : directory_{std::move(directory)} {}

    [[nodiscard]] PaperTransportResult run() const;
    [[nodiscard]] PaperTransportResult run_control() const;

  private:
    std::filesystem::path directory_;
};
} // namespace trading
