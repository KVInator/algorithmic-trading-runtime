#pragma once
#include "paper_broker_diagnostic.hpp"

namespace trading {
class PaperBrokerReport {
  public:
    explicit PaperBrokerReport(const PaperBrokerResult &result) : result_{result} {}

    void write(const std::filesystem::path &directory) const;

  private:
    const PaperBrokerResult &result_;
};
} // namespace trading
