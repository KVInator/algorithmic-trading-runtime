#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace trading {

struct AccountValue {
    double cash_usd;
    double shares;
    double receivable_usd;
    double liquid_wealth_usd;
    double equity_usd;
};

struct AccountEvent {
    std::chrono::sys_seconds at;
    std::string kind;
    std::string action_id;
    double reference_price_usd;
    double traded_notional_usd;
    double cost_usd;
    AccountValue value;
};

// Fractional, unlevered, single-instrument USD account. Raw reference prices;
// no interest, settlement delay, taxes or broker-fill simulation. Unpaid
// dividends enter equity but cannot finance purchases. Events are chronological.
class LongCashAccount {
public:
    explicit LongCashAccount(double initial_cash_usd);
    [[nodiscard]] AccountValue value(double raw_price_usd) const;
    void mark(std::chrono::sys_seconds at, double raw_price_usd);
    void rebalance(std::chrono::sys_seconds at, double raw_price_usd,
                   double target_liquid_weight, double cost_bps);
    // Corporate actions occur at the start of an ex/effective UTC date,
    // before any intraday trade. Same-date splits must precede dividends;
    // the caller supplies the dividend amount in the resulting share units.
    void accrue_dividend(std::chrono::sys_seconds ex_date, std::string id,
                         std::chrono::sys_days pay_date, double usd_per_share,
                         double ex_date_raw_price_usd);
    void settle_dividends(std::chrono::sys_seconds at, double raw_price_usd);
    void split(std::chrono::sys_seconds effective_date, std::string id,
               double new_shares_per_old_share, double pre_split_price_usd);
    [[nodiscard]] const std::vector<AccountEvent>& ledger() const noexcept;
private:
    struct Receivable { std::chrono::sys_days pay_date; double amount; };
    void check_time(std::chrono::sys_seconds at) const;
    void check_action(std::chrono::sys_seconds at, const std::string& id) const;
    void record(std::chrono::sys_seconds at, std::string kind, std::string id,
                double price, double notional, double cost, const AccountValue& next);
    double cash_;
    double shares_{0};
    double receivable_{0};
    std::optional<std::chrono::sys_seconds> last_;
    std::set<std::string> action_ids_;
    std::map<std::string, Receivable> unpaid_;
    std::vector<AccountEvent> ledger_;
};

struct AccountingCheck {
    std::string name;
    double actual;
    double expected;
    double tolerance;
    bool passed;
};
struct AccountingDemonstration {
    std::vector<AccountEvent> ledger;
    std::vector<AccountingCheck> checks;
};
[[nodiscard]] AccountingDemonstration demonstrate_long_cash_account();

} // namespace trading
