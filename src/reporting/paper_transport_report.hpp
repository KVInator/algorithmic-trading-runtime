#pragma once
#include "paper_transport_result.hpp"

namespace trading {
class PaperTransportReport {
  public:
    explicit PaperTransportReport(const PaperTransportResult &result) : result_{result} {}

    void write(const std::filesystem::path &directory) const;

  private:
    const PaperTransportResult &result_;
};
} // namespace trading
