#pragma once

#include "runtime_input.hpp"

namespace trading::execution {

struct DiagnosticStep { RuntimeInput input; Outcome expected_outcome; };
struct ExecutionScenario {
    std::string name;
    std::vector<DiagnosticStep> steps;
    Cents expected_cash_cents;
    Shares expected_position_shares;
};
struct ExecutionDiagnosticResult {
    ExecutionScenario scenario;
    std::vector<TraceEntry> trace;
    AccountSnapshot final_account;
};

// Scripted delivery schedules, not a market matching or liquidity simulation.
// Throws if an expected outcome, final balance or independent replay differs.
class ExecutionDiagnostic {
public:
    [[nodiscard]] std::vector<ExecutionDiagnosticResult> run() const;
};

} // namespace trading::execution
