#pragma once

#include "order_runtime.hpp"

namespace trading::execution {

struct TopOfBook {
    std::uint64_t sequence;
    std::int64_t at_seconds;
    Cents bid_cents;
    Cents ask_cents;
    Shares bid_shares;
    Shares ask_shares;
    bool operator==(const TopOfBook&) const = default;
};
struct SimulatedFill {
    std::string execution_id;
    std::string order_id;
    Side side;
    Shares shares;
    Cents price_cents;
    Cents fee_units;
    std::uint64_t quote_sequence;
    bool operator==(const SimulatedFill&) const = default;
};

// Deterministic top-of-book budget, FIFO across accepted client orders. Quote
// duplicates never replenish liquidity. Generated reports are delivered separately.
class SimulatedBroker {
public:
    SimulatedBroker(std::string symbol, Cents cash_units, Cents fee_per_share_cents);
    void advance_to(std::int64_t now_seconds);
    [[nodiscard]] bool on_quote(const TopOfBook& quote);
    [[nodiscard]] bool submit(const OrderRequest& request, std::int64_t now_seconds);
    [[nodiscard]] bool cancel(const std::string& order_id, const std::string& cancel_id, std::int64_t now_seconds);
    void complete_cancels(std::int64_t now_seconds);
    [[nodiscard]] BrokerSnapshot snapshot() const;
    [[nodiscard]] std::optional<TopOfBook> quote() const;
    [[nodiscard]] const std::vector<BrokerReport>& reports() const noexcept;
    [[nodiscard]] const std::vector<SimulatedFill>& fills() const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] std::int64_t now_seconds() const noexcept;
    [[nodiscard]] std::string_view symbol() const noexcept;
private:
    struct Order { OrderRequest request; Shares filled; bool active; };
    void advance_time(std::int64_t now);
    void match();
    void report(const std::string& order_id, ReportKind kind, std::string execution_id = "",
                Shares quantity = 0, Cents price = 0, Cents fee = 0, Shares cumulative = 0, std::string cancel_id = "");
    std::string symbol_;
    Cents initial_cash_;
    Cents fee_per_share_;
    std::int64_t now_{0};
    std::map<std::uint64_t, TopOfBook> quotes_;
    std::optional<TopOfBook> quote_;
    Shares bid_remaining_{0};
    Shares ask_remaining_{0};
    std::map<std::string, Order> orders_;
    std::vector<std::string> fifo_;
    std::map<std::string, std::string> cancel_ids_;
    std::vector<std::pair<std::string, std::string>> pending_cancels_;
    std::vector<BrokerReport> reports_;
    std::vector<SimulatedFill> fills_;
};

} // namespace trading::execution
