#pragma once

#include "execution_diagnostic.hpp"

#include <filesystem>
#include <vector>

namespace trading::execution {

void write_execution_diagnostic_report(const std::filesystem::path& directory,
                                       const std::vector<ExecutionDiagnosticResult>& results);

} // namespace trading::execution
