#pragma once
#include "journal_frame.hpp"
#include "simulated_broker.hpp"
#include <memory>

namespace trading::execution {
inline constexpr MoneyUnits broker_fee_units = 10000;
inline constexpr MoneyUnits broker_initial_cash_units = 100000000000000;
// Stateful peer model. Its ledger comes only from its own accepted commands.
class BrokerState {
  public:
    explicit BrokerState(std::string account_id);
    [[nodiscard]] nlohmann::json mutate(const nlohmann::json &command);
    [[nodiscard]] nlohmann::json read(const nlohmann::json &command) const;
    [[nodiscard]] nlohmann::json snapshot() const;

  private:
    [[nodiscard]] nlohmann::json apply(const nlohmann::json &command);
    [[nodiscard]] nlohmann::json order(const SimulatedOrderView &view) const;
    void publish(std::size_t previous_reports);
    std::string account_id_;
    SimulatedBroker broker_;
    std::string session_date_;
    std::int64_t session_open_{}, session_close_{};
    nlohmann::json activities_ = nlohmann::json::array();
    std::map<std::string, std::pair<nlohmann::json, nlohmann::json>> commands_;
};
class BrokerService {
  public:
    [[nodiscard]] static BrokerService create(std::unique_ptr<JournalStorage> storage,
                                              std::string account_id);
    [[nodiscard]] static BrokerService recover(std::unique_ptr<JournalStorage> storage,
                                               std::string account_id);
    [[nodiscard]] static BrokerService recover_prefix(std::unique_ptr<JournalStorage> source,
                                                      std::unique_ptr<JournalStorage> destination,
                                                      std::string account_id);
    [[nodiscard]] nlohmann::json exchange(const nlohmann::json &command);
    [[nodiscard]] bool healthy() const noexcept;
    [[nodiscard]] nlohmann::json snapshot() const;
    BrokerService(BrokerService &&) noexcept = default;
    BrokerService &operator=(BrokerService &&) noexcept = default;
    BrokerService(const BrokerService &) = delete;

  private:
    BrokerService(std::unique_ptr<JournalStorage> storage, BrokerState state, std::size_t records,
                  std::size_t bytes, std::string digest);
    std::unique_ptr<JournalStorage> storage_;
    BrokerState state_;
    std::size_t records_{}, bytes_{};
    std::string digest_;
    bool healthy_{true};
};
class BrokerPeer {
  public:
    virtual ~BrokerPeer() = default;
    [[nodiscard]] virtual nlohmann::json exchange(const nlohmann::json &command) = 0;
};
class InProcessBrokerPeer final : public BrokerPeer {
  public:
    explicit InProcessBrokerPeer(BrokerService service) : service_(std::move(service)) {}
    [[nodiscard]] nlohmann::json exchange(const nlohmann::json &command) override {
        return service_.exchange(command);
    }

  private:
    BrokerService service_;
};
} // namespace trading::execution
