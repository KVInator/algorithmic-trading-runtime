#pragma once

#include "pipeline_diagnostic.hpp"

#include <filesystem>

namespace trading::execution {

// The result must outlive the report; export does not modify its inputs.
class PipelineDiagnosticReport {
public:
    explicit PipelineDiagnosticReport(const PipelineDiagnosticResult& result) : result_{result} {}
    void write(const std::filesystem::path& directory) const;

private:
    const PipelineDiagnosticResult& result_;
};

} // namespace trading::execution
