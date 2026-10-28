#pragma once
#include "order_runtime.hpp"

namespace trading::execution {
struct TopOfBook {
    std::uint64_t sequence;
    std::int64_t at_seconds;
    // Legacy field names. Units are selected by the owning contract: cents
    // for the v1 peer, nanodollars for the portfolio peer. No implicit scaling.
    Cents bid_cents, ask_cents;
    Shares bid_shares, ask_shares;
    bool operator==(const TopOfBook &) const = default;
};
struct SimulatedFill {
    std::string execution_id, order_id;
    Side side;
    Shares shares;
    Cents price_cents, fee_units;
    std::uint64_t quote_sequence;
    std::string symbol{};
    bool operator==(const SimulatedFill &) const = default;
};
enum class BrokerTimeInForce { day, ioc, gtc };
[[nodiscard]] std::string_view name(BrokerTimeInForce value);
struct SimulatedOrderView {
    OrderRequest request;
    Shares filled;
    bool active, accepted;
    BrokerTimeInForce time_in_force;
    std::int64_t expires_at_seconds;
};
struct QuoteBudget {
    std::optional<TopOfBook> quote;
    Shares bid_remaining{}, ask_remaining{};
    std::map<std::uint64_t, TopOfBook> history;
};
// One FIFO matcher serves both legacy and portfolio peers. Portfolio mode adds
// global admission, explicit lifecycle policies and per-symbol quote budgets.
class SimulatedBroker {
  public:
    SimulatedBroker(std::string symbol, Cents cash_units, Cents fee_per_share_units);
    SimulatedBroker(std::vector<std::string> symbols, Cents cash_units, Cents fee_per_share_units);
    void advance_to(std::int64_t now_seconds);
    [[nodiscard]] bool on_quote(const TopOfBook &quote);
    [[nodiscard]] bool on_quote(const std::string &symbol, const TopOfBook &quote,
                                std::int64_t received_at_seconds);
    [[nodiscard]] bool submit(const OrderRequest &request, std::int64_t now_seconds);
    [[nodiscard]] bool submit(const OrderRequest &request, std::int64_t now_seconds, BrokerTimeInForce tif,
                              std::int64_t expires_at_seconds);
    [[nodiscard]] bool cancel(const std::string &order_id, const std::string &cancel_id,
                              std::int64_t now_seconds);
    void complete_cancels(std::int64_t now_seconds);
    [[nodiscard]] BrokerSnapshot snapshot() const;
    [[nodiscard]] PortfolioBrokerSnapshot portfolio_snapshot() const;
    [[nodiscard]] MoneyUnits reserved_cash_units() const;
    [[nodiscard]] std::optional<TopOfBook> quote() const;
    [[nodiscard]] const std::map<std::string, QuoteBudget> &quote_budgets() const noexcept;
    [[nodiscard]] std::vector<SimulatedOrderView> order_views() const;
    [[nodiscard]] const std::vector<BrokerReport> &reports() const noexcept;
    [[nodiscard]] const std::vector<SimulatedFill> &fills() const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] std::int64_t now_seconds() const noexcept;
    [[nodiscard]] std::string_view symbol() const noexcept;

  private:
    void advance_time(std::int64_t now);
    void match(const std::string &symbol);
    void report(const std::string &order_id, ReportKind kind, std::string execution_id = "",
                Shares quantity = 0, Cents price = 0, Cents fee = 0, Shares cumulative = 0,
                std::string cancel_id = "");
    std::string symbol_;
    Cents initial_cash_, fee_per_share_;
    bool portfolio_{false};
    std::int64_t now_{};
    std::map<std::string, QuoteBudget> books_;
    std::map<std::string, SimulatedOrderView> orders_;
    std::vector<std::string> fifo_;
    std::map<std::string, std::string> cancel_ids_;
    std::vector<std::pair<std::string, std::string>> pending_cancels_;
    std::vector<BrokerReport> reports_;
    std::vector<SimulatedFill> fills_;
};
} // namespace trading::execution
