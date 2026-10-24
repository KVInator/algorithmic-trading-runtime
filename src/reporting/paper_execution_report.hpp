#pragma once
#include "paper_execution_diagnostic.hpp"

namespace trading::execution {
class PaperExecutionReport {
public:
    explicit PaperExecutionReport(const PaperBridgeResult& result) : result_{result} {}

    void write(const std::filesystem::path& directory) const;

private:
    const PaperBridgeResult& result_;
};
} // namespace trading::execution
