#pragma once
#include "alpaca_paper_contract.hpp"
#include "journaled_runtime.hpp"
#include <nlohmann/json.hpp>

namespace trading::execution {
struct PaperExecutionConfiguration {
    std::string account_id;
    std::string initial_cash_usd;
    RuntimeLimits limits;
    std::string fee_policy{"paper_zero_fees_v1"};
};

struct PaperOutcome {
    Outcome outcome;
    std::string reason;
};

// Schema-3 owner of the shared integer order engine. All monetary units are
// USD 1e-9. Cent-valued submit intents are converted once at this boundary.
class PaperExecutionState {
public:
    explicit PaperExecutionState(const PaperExecutionConfiguration& configuration);
    [[nodiscard]] PaperOutcome apply(const nlohmann::json& command);
    [[nodiscard]] AccountSnapshot account() const;
    [[nodiscard]] nlohmann::json snapshot() const;

private:
    [[nodiscard]] PaperOutcome handle(const nlohmann::json& command);
    [[nodiscard]] OrderView bind(const PaperOrder& order);
    [[nodiscard]] PaperOutcome ingress(const PaperStreamMessage& message);
    PaperExecutionConfiguration configuration_;
    OrderRuntime core_;
    bool authorized_{false};
    bool subscribed_{false};
    bool unresolved_{false};
    std::map<std::string, std::string> bindings_;
    std::map<std::string, std::string> executions_;
    std::map<std::string, std::string> events_;
};

class JournaledPaperRuntime {
public:
    [[nodiscard]] static JournaledPaperRuntime create(std::unique_ptr<JournalStorage> storage,
                                                      const PaperExecutionConfiguration& config);
    [[nodiscard]] static JournaledPaperRuntime recover(std::unique_ptr<JournalStorage> storage,
                                                       const PaperExecutionConfiguration& config);
    [[nodiscard]] static JournaledPaperRuntime recover_prefix(std::unique_ptr<JournalStorage> source,
                                                              std::unique_ptr<JournalStorage> destination,
                                                              const PaperExecutionConfiguration& config);
    JournaledPaperRuntime(JournaledPaperRuntime&&) noexcept = default;
    JournaledPaperRuntime& operator=(JournaledPaperRuntime&&) noexcept = default;
    JournaledPaperRuntime(const JournaledPaperRuntime&) = delete;
    JournaledPaperRuntime& operator=(const JournaledPaperRuntime&) = delete;
    [[nodiscard]] PaperOutcome submit(const OrderRequest& cent_intent);
    [[nodiscard]] PaperOutcome request_cancel(std::string client_id, std::string cancel_id);
    [[nodiscard]] PaperOutcome receive(std::string raw_message, std::string received_at);
    [[nodiscard]] PaperOutcome observe_order(std::string raw_order);
    [[nodiscard]] PaperOutcome reconcile(std::string account, std::string positions, std::string orders);
    [[nodiscard]] PaperOutcome disconnect();
    [[nodiscard]] nlohmann::json snapshot() const;
    [[nodiscard]] bool healthy() const noexcept;

private:
    JournaledPaperRuntime(std::unique_ptr<JournalStorage> storage, PaperExecutionState state,
                          std::size_t count, std::size_t bytes, std::string digest);
    [[nodiscard]] PaperOutcome execute(const nlohmann::json& command);
    void ensure_healthy() const;
    std::unique_ptr<JournalStorage> storage_;
    PaperExecutionState state_;
    std::size_t count_;
    std::size_t bytes_;
    std::string digest_;
    bool healthy_{true};
};
} // namespace trading::execution
