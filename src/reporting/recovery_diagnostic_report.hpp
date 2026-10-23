#pragma once

#include "recovery_diagnostic.hpp"

#include <filesystem>

namespace trading::execution {

// The result must outlive the report; export does not modify its inputs.
class RecoveryDiagnosticReport {
public:
    explicit RecoveryDiagnosticReport(const RecoveryDiagnosticResult& result) : result_{result} {}
    void write(const std::filesystem::path& directory) const;

private:
    const RecoveryDiagnosticResult& result_;
};

} // namespace trading::execution
