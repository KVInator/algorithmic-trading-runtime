#pragma once
#include "extension_contract.hpp"
#include "order_runtime.hpp"
#include <variant>

namespace trading::execution {
inline constexpr MoneyUnits shared_usd_scale = 1000000000;
struct SharedCapitalConfiguration {
    std::vector<std::string> symbols, families;
    MoneyUnits initial_cash_units;
    MoneyUnits fee_per_share_ceiling_units{};
    std::int64_t maximum_quote_age_seconds{2}, dispatch_window_seconds{30};
    [[nodiscard]] static SharedCapitalConfiguration registered(const ExtensionStudyContract& contract);
    bool operator==(const SharedCapitalConfiguration&) const = default;
};
struct OwnerKey {
    std::string family, symbol;
    auto operator<=>(const OwnerKey&) const = default;
};
struct OwnerPosition {
    Shares shares{}, desired_shares{};
    MoneyUnits attribution_cash_flow_units{};
    std::int64_t decision_at_seconds{}, expires_at_seconds{};
    bool operator==(const OwnerPosition&) const = default;
};
struct SharedQuote {
    std::string symbol;
    std::int64_t source_at_seconds;
    MoneyUnits bid_units, ask_units;
    bool operator==(const SharedQuote&) const = default;
};
struct FamilySignal {
    OwnerKey owner;
    bool long_target;
    bool operator==(const FamilySignal&) const = default;
};
struct SharedTargetBatch {
    std::string id;
    std::int64_t at_seconds;
    std::vector<SharedQuote> quotes;
    std::vector<FamilySignal> signals;
    bool operator==(const SharedTargetBatch&) const = default;
};
struct SharedDelivery { std::int64_t at_seconds; BrokerReport report; };
struct SharedConnection { std::int64_t at_seconds; bool connected; };
struct SharedReconciliation { std::int64_t at_seconds; PortfolioBrokerSnapshot broker; };
struct SharedSeal { std::int64_t at_seconds; };
using SharedInput = std::variant<SharedTargetBatch, SharedDelivery, SharedConnection, SharedReconciliation, SharedSeal>;
struct OwnerAllocation { OwnerKey owner; Shares shares; MoneyUnits fee_units{}; };
struct OwnershipTransfer { OwnerKey from, to; Shares shares; MoneyUnits price_units; };
struct SharedOrderPlan {
    OrderRequest request;
    std::map<OwnerKey, Shares> remaining;
};
struct SharedResult {
    Outcome outcome;
    std::string reason;
    std::vector<OrderRequest> dispatch_orders{};
    std::vector<std::pair<std::string, std::string>> dispatch_cancels{};
    std::vector<OwnerAllocation> fills{};
    std::vector<OwnershipTransfer> transfers{};
};
struct SharedCapitalSnapshot {
    PortfolioAccountSnapshot account;
    std::map<OwnerKey, OwnerPosition> owners;
    std::map<std::string, SharedQuote> quotes;
    std::map<std::string, SharedOrderPlan> open_plans;
    std::vector<OrderView> open_orders;
    MoneyUnits equity_units;
    std::vector<std::string> budget_breaches;
    std::int64_t at_seconds;
    std::uint64_t generation;
    bool sealed;
};
struct SharedFlatCheckpoint {
    MoneyUnits cash_units;
    std::map<OwnerKey, MoneyUnits> attribution_cash_flows;
    std::int64_t at_seconds;
    bool operator==(const SharedFlatCheckpoint&) const = default;
};

// One authoritative cash/order core, with symbol inventories and strategy
// attribution. Commands are serialized; the journal owns durable publication.
class SharedCapitalBook {
public:
    explicit SharedCapitalBook(SharedCapitalConfiguration configuration, std::uint64_t generation = 0,
        std::optional<SharedFlatCheckpoint> checkpoint = {});
    [[nodiscard]] SharedResult apply(const SharedInput& input);
    [[nodiscard]] SharedCapitalSnapshot snapshot() const;
    [[nodiscard]] SharedFlatCheckpoint flat_checkpoint() const;
    [[nodiscard]] const SharedCapitalConfiguration& configuration() const noexcept;
private:
    [[nodiscard]] SharedResult handle(const SharedTargetBatch& input);
    [[nodiscard]] SharedResult handle(const SharedDelivery& input);
    [[nodiscard]] SharedResult handle(const SharedConnection& input);
    [[nodiscard]] SharedResult handle(const SharedReconciliation& input);
    [[nodiscard]] SharedResult handle(const SharedSeal& input);
    void route(SharedResult& result);
    void validate() const;
    [[nodiscard]] MoneyUnits mark(const std::string& symbol) const;
    [[nodiscard]] MoneyUnits exposure(const std::optional<std::string>& family,
        const std::optional<std::string>& symbol, const std::optional<OwnerKey>& owner = {}) const;
    [[nodiscard]] bool fresh_quotes() const;
    [[nodiscard]] Shares desired(const OwnerKey& owner) const;
    SharedCapitalConfiguration configuration_;
    OrderRuntime core_;
    std::map<OwnerKey, OwnerPosition> owners_;
    std::map<std::string, SharedQuote> quotes_;
    std::map<std::string, SharedOrderPlan> plans_;
    std::map<std::string, SharedTargetBatch> batches_;
    std::int64_t at_seconds_{};
    std::uint64_t generation_{}, next_order_{}, next_cancel_{};
    bool sealed_{};
};
} // namespace trading::execution
