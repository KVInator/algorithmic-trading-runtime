#pragma once

#include "recovery_diagnostic.hpp"

namespace trading::execution {
void write_recovery_diagnostic_report(const std::filesystem::path& directory, const RecoveryDiagnosticResult& result);
} // namespace trading::execution
