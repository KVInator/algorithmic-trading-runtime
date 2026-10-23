#pragma once

#include "order_runtime.hpp"

#include <variant>

namespace trading::execution {

struct SubmitInput { OrderRequest request; };
struct CancelInput { std::string order_id; std::string cancel_id; };
struct ConnectionInput { bool connected; };
using RuntimeInput = std::variant<SubmitInput, CancelInput, BrokerReport, ConnectionInput, BrokerSnapshot>;
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
[[nodiscard]] std::vector<ExecutionDiagnosticResult> run_execution_diagnostic();

} // namespace trading::execution
