#include "long_cash_account.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

double bounded(long double value) {
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<double>::max()) {
        throw std::overflow_error("account value is negative or not representable in USD/share units");
    }
    return static_cast<double>(value);
}
void positive(double x, const char* message) {
    if (!std::isfinite(x) || x <= 0) { throw std::invalid_argument(message); }
}
AccountValue valuation(double cash, double shares, double receivable, double price) {
    positive(price, "raw reference price must be finite and positive");
    const auto liquid = bounded(static_cast<long double>(cash) + static_cast<long double>(shares) * price);
    return {cash, shares, receivable, liquid, bounded(static_cast<long double>(liquid) + receivable)};
}

} // namespace

LongCashAccount::LongCashAccount(double initial_cash_usd) : cash_{initial_cash_usd} {
    positive(cash_, "initial cash must be finite and positive");
}
AccountValue LongCashAccount::value(double price) const { return valuation(cash_, shares_, receivable_, price); }
const std::vector<AccountEvent>& LongCashAccount::ledger() const noexcept { return ledger_; }

void LongCashAccount::check_time(std::chrono::sys_seconds at) const {
    if (last_ && at < *last_) { throw std::invalid_argument("account events cannot move backwards in time"); }
}
void LongCashAccount::check_action(std::chrono::sys_seconds at, const std::string& id) const {
    check_time(at);
    if (at != std::chrono::floor<std::chrono::days>(at) || id.empty() || action_ids_.contains(id)) {
        throw std::invalid_argument("corporate action requires a unique ID and a start-of-date timestamp");
    }
    for (auto item = ledger_.rbegin(); item != ledger_.rend() && item->at == at; ++item) {
        if (item->kind == "buy" || item->kind == "sell" || item->kind == "hold") {
            throw std::invalid_argument("corporate actions must precede same-time trades");
        }
    }
}
void LongCashAccount::record(std::chrono::sys_seconds at, std::string kind, std::string id,
                            double price, double notional, double cost, const AccountValue& next) {
    ledger_.push_back({at, std::move(kind), std::move(id), price, notional, cost, next});
    cash_ = next.cash_usd;
    shares_ = next.shares;
    receivable_ = next.receivable_usd;
    last_ = at;
}
void LongCashAccount::mark(std::chrono::sys_seconds at, double price) {
    check_time(at);
    record(at, "mark", "", price, 0, 0, value(price));
}
void LongCashAccount::rebalance(std::chrono::sys_seconds at, double price, double weight, double cost_bps) {
    check_time(at);
    if (!std::isfinite(weight) || weight < 0 || weight > 1 || !std::isfinite(cost_bps) ||
        cost_bps < 0 || cost_bps >= 10000) {
        throw std::invalid_argument("long/cash weight requires [0,1] and cost requires [0,10000) bp");
    }
    for (const auto& [id, due] : unpaid_) {
        static_cast<void>(id);
        if (due.pay_date <= std::chrono::floor<std::chrono::days>(at)) {
            throw std::logic_error("settle due dividends before rebalancing");
        }
    }
    static_cast<void>(value(price));
    const long double stock = static_cast<long double>(shares_) * price;
    const long double liquid = static_cast<long double>(cash_) + stock;
    const long double gap = static_cast<long double>(weight) * liquid - stock;
    const long double rate = cost_bps / 10000.0L;
    // Solve stock_after = weight * (liquid_before - fee) on the charged side.
    const long double signed_notional = gap / (1 + (gap >= 0 ? rate : -rate) * weight);
    const long double fee = std::abs(signed_notional) * rate;
    long double cash = static_cast<long double>(cash_) - signed_notional - fee;
    long double stock_after = stock + signed_notional;
    const auto tolerance = 32 * std::numeric_limits<double>::epsilon() * liquid;
    if (cash < 0 && cash >= -tolerance) { cash = 0; }
    if (stock_after < 0 && stock_after >= -tolerance) { stock_after = 0; }
    const auto next = valuation(bounded(cash), bounded(stock_after / price), receivable_, price);
    record(at, signed_notional > 0 ? "buy" : signed_notional < 0 ? "sell" : "hold", "",
           price, bounded(std::abs(signed_notional)), bounded(fee), next);
}
void LongCashAccount::accrue_dividend(std::chrono::sys_seconds at, std::string id,
                                    std::chrono::sys_days pay_date, double amount, double price) {
    check_action(at, id);
    positive(amount, "cash dividend per share must be finite and positive");
    if (pay_date < std::chrono::floor<std::chrono::days>(at)) {
        throw std::invalid_argument("dividend pay date cannot precede ex-date");
    }
    const auto entitlement = bounded(static_cast<long double>(shares_) * amount);
    const auto next = valuation(cash_, shares_, bounded(static_cast<long double>(receivable_) + entitlement), price);
    unpaid_.emplace(id, Receivable{pay_date, entitlement});
    action_ids_.insert(id);
    record(at, "dividend_ex", std::move(id), price, 0, 0, next);
}
void LongCashAccount::settle_dividends(std::chrono::sys_seconds at, double price) {
    check_time(at);
    static_cast<void>(value(price));
    long double paid = 0;
    for (const auto& [id, due] : unpaid_) {
        static_cast<void>(id);
        if (due.pay_date <= std::chrono::floor<std::chrono::days>(at)) { paid += due.amount; }
    }
    // Sum the remaining claims directly; repeated subtraction can leave a
    // spurious negative receivable after several independently rounded claims.
    long double remaining = 0;
    for (const auto& [id, due] : unpaid_) {
        static_cast<void>(id);
        if (due.pay_date > std::chrono::floor<std::chrono::days>(at)) { remaining += due.amount; }
    }
    const auto next = valuation(bounded(static_cast<long double>(cash_) + paid), shares_, bounded(remaining), price);
    record(at, "dividend_payment", "", price, 0, 0, next);
    std::erase_if(unpaid_, [&](const auto& item) { return item.second.pay_date <= std::chrono::floor<std::chrono::days>(at); });
}
void LongCashAccount::split(std::chrono::sys_seconds at, std::string id, double ratio, double price) {
    check_action(at, id);
    positive(ratio, "split ratio must be finite and positive");
    static_cast<void>(value(price));
    for (auto item = ledger_.rbegin(); item != ledger_.rend() && item->at == at; ++item) {
        if (item->kind == "dividend_ex") {
            throw std::invalid_argument("same-date splits must precede dividend entitlement");
        }
    }
    const auto after_price = bounded(static_cast<long double>(price) / ratio);
    const auto next = valuation(cash_, bounded(static_cast<long double>(shares_) * ratio), receivable_, after_price);
    action_ids_.insert(id);
    record(at, "split", std::move(id), after_price, 0, 0, next);
}

AccountingDemonstration demonstrate_long_cash_account() {
    using namespace std::chrono;
    const sys_days day{year{2022}/January/3};
    LongCashAccount account{10000};
    AccountingDemonstration result;
    auto check = [&](std::string name, double actual, double expected) {
        const double tolerance = 1e-9; // USD identities on an initial USD 10,000 account.
        result.checks.push_back({std::move(name), actual, expected, tolerance, std::abs(actual - expected) <= tolerance});
    };
    account.mark(day + hours{14}, 100);
    account.rebalance(day + hours{14} + minutes{31}, 100, .5, 1);
    const auto bought = account.value(100);
    check("Post-cost stock weight", bought.shares * 100 / bought.liquid_wealth_usd, .5);
    check("Purchase fee on actual notional", account.ledger().back().cost_usd, 5000.0 / 1.00005 * .0001);
    account.accrue_dividend(day + days{1}, "constructed-dividend", day + days{3}, 1, 99);
    check("Ex-date price drop and receivable conserve equity", account.value(99).equity_usd, bought.equity_usd);
    account.rebalance(day + days{1} + hours{14} + minutes{31}, 99, 0, 1);
    const auto sold = account.value(99);
    check("Receivable survives stock liquidation", sold.receivable_usd, bought.shares);
    account.settle_dividends(day + days{3}, 99);
    check("Payment transfers wealth without income duplication", account.value(99).equity_usd, sold.equity_usd);
    account.settle_dividends(day + days{4}, 99);
    check("Repeated settlement cannot pay twice", account.value(99).cash_usd, sold.equity_usd);
    result.ledger = account.ledger();
    LongCashAccount split_account{10000};
    split_account.rebalance(day, 100, 1, 0);
    split_account.split(day + days{1}, "constructed-split", 2, 100);
    check("Two-for-one split conserves equity", split_account.value(50).equity_usd, 10000);
    check("Two-for-one split doubles shares", split_account.value(50).shares, 200);
    for (const auto& item : result.checks) {
        if (!item.passed) { throw std::logic_error("synthetic accounting identity failed: " + item.name); }
    }
    return result;
}

} // namespace trading
