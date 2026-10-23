#pragma once

#include "pipeline_diagnostic.hpp"

namespace trading::execution {
void write_pipeline_diagnostic_report(const std::filesystem::path& directory, const PipelineDiagnosticResult& result);
} // namespace trading::execution
