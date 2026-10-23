#pragma once

#include "execution_diagnostic.hpp"

#include <filesystem>

namespace trading::execution {

// The result must outlive the report; export does not modify its inputs.
class ExecutionDiagnosticReport {
public:
    explicit ExecutionDiagnosticReport(const std::vector<ExecutionDiagnosticResult>& result) : result_{result} {}
    void write(const std::filesystem::path& directory) const;

private:
    const std::vector<ExecutionDiagnosticResult>& result_;
};

} // namespace trading::execution
