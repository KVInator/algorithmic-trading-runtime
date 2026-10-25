#pragma once

#include "offline_pipeline.hpp"
#include "strategy_order_adapters.hpp"
#include "development_dataset.hpp"

namespace trading::execution {
struct PipelineCheck {
    std::string name;
    bool passed;
};
enum class ExecutionDesign { momentum, continuation_price, continuation_volume, reversion };
[[nodiscard]] std::string_view name(ExecutionDesign design);

struct ExecutionFrame {
    std::string label;
    std::int64_t at_seconds;
    AccountSnapshot account;
    BrokerSnapshot broker;
    std::vector<OrderView> orders;
    std::optional<TargetIntent> target;
    std::optional<TopOfBook> quote;
    std::optional<TraceEntry> last_transition;
    std::size_t broker_fill_count;
    std::size_t broker_report_count;
    std::size_t journal_records;
    std::size_t client_restarts;
};

struct ExecutionParity {
    std::string convention;
    double fractional_entry_shares;
    double fractional_final_equity_usd;
    double integer_final_equity_usd;
    double integer_idle_cash_after_entry_usd;
    double difference_usd;
    bool exact_alignment;
};

struct StrategyExecutionCase {
    ExecutionDesign design;
    std::string label;
    std::string journal_file;
    std::string signal_reason;
    bool historical_proxy;
    std::vector<TopOfBook> quotes;
    std::vector<SimulatedFill> fills;
    std::vector<BrokerReport> reports;
    std::vector<ExecutionFrame> frames;
    std::optional<ExecutionParity> parity;
};

struct StrategyExecutionInputs {
    std::vector<SessionDownload> history;
    std::vector<MinuteBar> continuation_long;
    std::vector<MinuteBar> continuation_cash;
    std::vector<MinuteBar> reversion_long;
    std::vector<MinuteBar> reversion_cash;
    std::vector<MinuteBar> momentum_long;
    std::vector<MinuteBar> momentum_cash;
    HistoricalBarRequest current;
};

struct StrategyExecutionResult {
    std::vector<PipelineCheck> checks;
    std::vector<StrategyExecutionCase> cases;
    StrategyExecutionInputs inputs;
    std::optional<std::string> historical_manifest;
    std::optional<StrategyExecutionInputs> historical_inputs;
};

class StrategyExecutionDiagnostic {
  public:
    explicit StrategyExecutionDiagnostic(std::filesystem::path directory)
        : directory_{std::move(directory)} {}

    [[nodiscard]] StrategyExecutionResult run() const;
    void historical(StrategyExecutionResult &result, const FrozenDevelopmentDataset &dataset) const;
    [[nodiscard]] static StrategyExecutionInputs constructed_inputs();

  private:
    std::filesystem::path directory_;
};
} // namespace trading::execution
