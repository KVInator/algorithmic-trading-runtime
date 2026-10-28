#pragma once
#include "broker_service.hpp"
#include <filesystem>

namespace trading::execution {
struct ProcessBrokerDiagnosticResult {
    std::filesystem::path directory;
    std::vector<std::string> checks;
    nlohmann::json cases, calls, rejected_files;
    std::vector<std::uint64_t> process_ids;
};
class ProcessBrokerDiagnostic {
  public:
    ProcessBrokerDiagnostic(std::filesystem::path root, std::filesystem::path executable);
    [[nodiscard]] ProcessBrokerDiagnosticResult run() const;

  private:
    std::filesystem::path root_, executable_;
};
} // namespace trading::execution
