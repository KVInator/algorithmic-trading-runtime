#pragma once
#include "broker_service.hpp"
#include "journaled_shared_capital.hpp"

namespace trading::execution {
// Client recovery consumes independent activity history before exact snapshot
// reconciliation. A failed exchange closes routing; submission is never retried.
class SharedBrokerSession {
  public:
    SharedBrokerSession(JournaledSharedCapital &runtime, BrokerPeer &peer, std::string account_id);
    [[nodiscard]] bool connect(std::int64_t receipt_at_seconds);
    [[nodiscard]] bool drain(std::int64_t receipt_at_seconds);
    [[nodiscard]] SharedResult target(const SharedTargetBatch &batch);
    void disconnect(std::int64_t receipt_at_seconds);
    [[nodiscard]] std::uint64_t cursor() const noexcept;
    [[nodiscard]] const std::string &last_fault() const noexcept;

  private:
    void catch_up(std::int64_t at, bool dispatch);
    void send(const SharedResult &result, std::int64_t at);
    void fault(std::int64_t at) noexcept;
    JournaledSharedCapital &runtime_;
    BrokerPeer &peer_;
    std::string account_id_;
    std::uint64_t cursor_{};
    bool active_{};
    std::string last_fault_;
};
} // namespace trading::execution
