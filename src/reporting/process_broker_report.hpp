#pragma once
#include "process_broker_diagnostic.hpp"

namespace trading {
class ProcessBrokerReport {
  public:
    void write(const execution::ProcessBrokerDiagnosticResult &result) const;
};
} // namespace trading
