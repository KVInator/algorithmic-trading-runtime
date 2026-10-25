#pragma once
#include "paper_transport.hpp"
#include "simulated_broker.hpp"
#include <deque>

namespace trading {
// In-process protocol peer. Matching and broker cash belong to SimulatedBroker;
// this class only translates requests/reports and controls message delivery.
class SimulatedPaperWire final : public PaperWire {
  public:
    explicit SimulatedPaperWire(std::string account_id, execution::Cents initial_cash_cents);
    [[nodiscard]] PaperHttpResponse request(const PaperRequest &) override;
    void open_authenticated_stream() override;
    void subscribe_trade_updates() override;
    void ping(std::string_view token, std::chrono::milliseconds) override;
    [[nodiscard]] std::optional<std::string> take_pong() override;
    [[nodiscard]] std::optional<std::string> poll(std::chrono::milliseconds) override;
    void close() noexcept override;
    void advance_to(std::int64_t source_seconds);
    void on_quote(const execution::TopOfBook &quote);
    void complete_cancels();
    void lose_next_submit_response() noexcept;
    void duplicate_last_update();
    void replay_trade_history();
    [[nodiscard]] nlohmann::json snapshot() const;
    [[nodiscard]] const std::vector<std::string> &trade_history() const noexcept;
    [[nodiscard]] std::size_t submissions() const noexcept;

  private:
    void publish();
    [[nodiscard]] std::string timestamp() const;
    [[nodiscard]] nlohmann::json account() const;
    [[nodiscard]] nlohmann::json positions() const;
    std::string account_id_;
    execution::SimulatedBroker broker_;
    std::map<std::string, nlohmann::json> orders_;
    std::vector<std::string> history_;
    nlohmann::json quotes_ = nlohmann::json::array();
    std::deque<std::string> messages_;
    std::optional<std::string> pong_;
    std::size_t consumed_reports_{0}, submissions_{0};
    execution::Shares reported_position_{0};
    bool opened_{false}, subscribed_{false}, lose_response_{false};
};
} // namespace trading
