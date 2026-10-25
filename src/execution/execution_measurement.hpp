#pragma once
#include "simulated_paper_wire.hpp"

namespace trading::execution {
struct ExecutionFillObservation {
    Shares quantity;
    Cents price_cents;
    Cents bid_cents;
    Cents ask_cents;
    std::uint64_t quote_sequence;
    std::int64_t source_seconds;
    std::int64_t receipt_seconds;
};
struct ExecutionObservation {
    Side side;
    Shares requested_quantity;
    Cents limit_cents;
    Cents arrival_bid_cents;
    Cents arrival_ask_cents;
    Cents terminal_bid_cents;
    Cents terminal_ask_cents;
    std::int64_t decision_seconds;
    std::int64_t dispatch_seconds;
    std::int64_t acknowledgement_source_seconds;
    std::int64_t acknowledgement_receipt_seconds;
    std::int64_t horizon_seconds;
    std::vector<ExecutionFillObservation> fills;
    std::optional<std::int64_t> cancellation_source_seconds{};
    std::optional<std::int64_t> cancellation_receipt_seconds{};
};
// Economic costs use signed half-cent integers; ratios are display/statistical
// values. Arrival means the client dispatch quote. Unfilled shares are marked
// at the common terminal midpoint, with the assumption stated in the report.
class ExecutionQuality {
  public:
    [[nodiscard]] nlohmann::json measure(const ExecutionObservation &observation) const;
};
struct ExecutionWorkload {
    std::string id;
    std::string path;
    Side side;
    Cents spread_cents;
    Shares quote_size;
    std::int64_t outbound_delay_seconds;
    std::int64_t delivery_delay_seconds;
    std::int64_t cancel_processing_seconds;
};
struct ExecutionPolicy {
    std::string id;
    bool aggressive_limit;
    std::int64_t cancel_after_seconds;
    bool operator==(const ExecutionPolicy &) const = default;
};
struct ExecutionMeasurementResult {
    nlohmann::json cases = nlohmann::json::array();
    nlohmann::json comparisons = nlohmann::json::array();
    nlohmann::json sensitivities = nlohmann::json::array();
    nlohmann::json policy_summaries = nlohmann::json::array();
};
class ExecutionMeasurementStudy {
  public:
    [[nodiscard]] static std::vector<ExecutionWorkload> workloads();
    [[nodiscard]] static std::vector<ExecutionPolicy> policies();
    [[nodiscard]] ExecutionMeasurementResult run(const std::filesystem::path &directory,
                                                 const std::vector<ExecutionWorkload> &workloads,
                                                 const std::vector<ExecutionPolicy> &policies) const;
};
} // namespace trading::execution
