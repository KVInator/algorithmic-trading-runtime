#pragma once

#include "momentum_order_adapter.hpp"
#include "offline_pipeline.hpp"

namespace trading::execution {

struct PipelineCheck { std::string name; bool passed; };
struct PipelineCase {
    std::string name;
    std::string journal_file;
    std::vector<TopOfBook> quotes;
    std::vector<TargetIntent> targets;
    std::vector<SimulatedFill> fills;
    std::vector<BrokerReport> broker_reports;
    std::vector<TraceEntry> runtime_trace;
    AccountSnapshot final_account;
    std::size_t client_restarts;
};
struct PipelineDiagnosticResult {
    std::vector<PipelineCheck> checks;
    std::vector<PipelineCase> cases;
    std::vector<MinuteBar> rising_prefix;
    std::vector<MinuteBar> falling_prefix;
};

[[nodiscard]] PipelineDiagnosticResult run_pipeline_diagnostic(const std::filesystem::path& directory);

} // namespace trading::execution
