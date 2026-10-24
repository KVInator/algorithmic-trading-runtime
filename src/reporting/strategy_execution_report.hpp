#pragma once

#include "strategy_execution_diagnostic.hpp"

namespace trading::execution {
class StrategyExecutionReport {
public:
    explicit StrategyExecutionReport(const StrategyExecutionResult& result) : result_{result} {}

    void write(const std::filesystem::path& directory) const;

private:
    const StrategyExecutionResult& result_;
};
} // namespace trading::execution
