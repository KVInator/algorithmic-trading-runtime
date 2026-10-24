#pragma once

#include "journal_storage.hpp"
#include "runtime_input.hpp"

#include <memory>
#include <stdexcept>

namespace trading::execution {

struct RuntimeConfiguration {
    std::string symbol;
    Cents initial_cash_units;
    RuntimeLimits limits;
};
struct JournalInspection {
    std::size_t input_records;
    std::size_t complete_bytes;
    std::size_t incomplete_tail_bytes;
    std::string last_sha256;
    int schema_version{1};
};
// Journal before acknowledging eligibility for external dispatch. Recovery
// reuses OrderRuntime, then durably disconnects before exposing any state.
// No automatic resend, in-place truncation or broker transport is provided.
class JournaledRuntime {
public:
    [[nodiscard]] static JournaledRuntime create(std::unique_ptr<JournalStorage> storage, const RuntimeConfiguration& config,
                                                 int schema_version = 1);
    [[nodiscard]] static JournaledRuntime recover(std::unique_ptr<JournalStorage> storage, const RuntimeConfiguration& expected);
    [[nodiscard]] static JournaledRuntime recover_prefix(std::unique_ptr<JournalStorage> source,
        std::unique_ptr<JournalStorage> destination, const RuntimeConfiguration& expected);
    [[nodiscard]] static JournalInspection inspect(JournalStorage& storage, const RuntimeConfiguration& expected);
    JournaledRuntime(JournaledRuntime&&) noexcept = default;
    JournaledRuntime& operator=(JournaledRuntime&&) noexcept = default;
    JournaledRuntime(const JournaledRuntime&) = delete;
    JournaledRuntime& operator=(const JournaledRuntime&) = delete;
    [[nodiscard]] Outcome execute(const RuntimeInput& input);
    [[nodiscard]] AccountSnapshot account() const;
    [[nodiscard]] std::string symbol() const;
    [[nodiscard]] OrderView order(const std::string& id) const;
    [[nodiscard]] std::vector<OrderView> orders() const;
    [[nodiscard]] std::optional<TargetIntent> latest_target() const;
    [[nodiscard]] const std::vector<TraceEntry>& trace() const;
    [[nodiscard]] JournalInspection inspection() const;
    [[nodiscard]] bool healthy() const noexcept;
private:
    JournaledRuntime(std::unique_ptr<JournalStorage> storage, OrderRuntime runtime, JournalInspection inspection);
    void ensure_healthy() const;
    std::unique_ptr<JournalStorage> storage_;
    OrderRuntime runtime_;
    JournalInspection inspection_;
    bool healthy_{true};
};

} // namespace trading::execution
