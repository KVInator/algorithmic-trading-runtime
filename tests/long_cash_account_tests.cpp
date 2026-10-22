#include "long_cash_account.hpp"
#include "intraday_momentum.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace std::chrono;
const sys_days day{year{2022}/January/3};
void require(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
void near(double actual, double expected, const char* message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > 2e-11 * std::max(1.0, std::abs(expected))) {
        throw std::runtime_error(message);
    }
}
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid account event must be rejected");
}

void test_rebalance_and_overnight() {
    for (const double cost : {0.0, .5, 1.0, 2.0, 5.0}) {
        trading::LongCashAccount a{10000};
        a.rebalance(day, 100, 1, cost);
        const auto bought = a.value(100);
        near(bought.cash_usd, 0, "full entry consumes available cash including fee");
        near(bought.shares, 10000 / (100 * (1 + cost / 10000)), "buy fee reduces affordable shares");
        a.mark(day + hours{21}, 101);
        a.mark(day + days{1} + hours{14}, 110);
        near(a.value(110).equity_usd / bought.equity_usd, 1.1, "overnight gap enters marked equity");
        a.rebalance(day + days{1} + hours{15}, 110, 0, cost);
        near(a.value(110).equity_usd / 10000 - 1,
             trading::evaluate_long_prices(100, 110, cost).net_return,
             "full round trip agrees with the established continuous-notional limit");
    }
    // An independent bisection of the self-financing equation checks both
    // charged sides without using the implementation's closed-form solution.
    for (const double initial_weight : {0.0, .3, 1.0}) {
        for (const double target : {0.0, .1, .5, .9, 1.0}) {
            for (const double cost : {0.0, 1.0, 5.0, 500.0}) {
                trading::LongCashAccount a{10000};
                a.rebalance(day, 100, initial_weight, 0);
                const auto before = a.value(137);
                const double stock = before.shares * 137;
                const double wealth = before.liquid_wealth_usd;
                double low = -stock, high = before.cash_usd / (1 + cost / 10000);
                for (int i = 0; i < 100; ++i) {
                    const double mid = (low + high) / 2;
                    const double residual = stock + mid - target * (wealth - std::abs(mid) * cost / 10000);
                    if (residual < 0) { low = mid; } else { high = mid; }
                }
                a.rebalance(day + days{1}, 137, target, cost);
                const auto after = a.value(137);
                const auto& trade = a.ledger().back();
                near(trade.traded_notional_usd, std::abs((low + high) / 2), "trade solves independent budget equation");
                near(after.shares * 137 / after.liquid_wealth_usd, target, "weight is measured after transaction costs");
                near(after.equity_usd, wealth - trade.cost_usd, "self-financing trade loses only its fee");
                require(after.cash_usd >= 0 && after.shares >= 0, "account remains unlevered");
            }
        }
    }
    trading::LongCashAccount flat{10000};
    flat.rebalance(day, 100, 0, 5);
    flat.mark(day + days{1}, 200);
    near(flat.value(200).equity_usd, 10000, "flat account does not earn stock returns");
    near(flat.ledger().front().cost_usd, 0, "flat hold has no fictitious fee");
    trading::LongCashAccount drift{10000};
    drift.rebalance(day, 100, .5, 0);
    drift.rebalance(day + days{1}, 120, .5, 1);
    require(drift.ledger().back().kind == "sell" && drift.ledger().back().cost_usd > 0,
            "same target weight trades after stock drift");
}

void test_actions_and_chronology() {
    trading::LongCashAccount a{10000};
    a.rebalance(day, 100, .5, 0);
    a.accrue_dividend(day + days{1}, "d1", day + days{3}, 1, 99);
    near(a.value(99).equity_usd, 10000, "ex-date receivable offsets the constructed price drop");
    near(a.value(99).liquid_wealth_usd, 9950, "unpaid dividend is not liquid wealth");
    a.rebalance(day + days{1} + hours{15}, 99, 1, 0);
    near(a.value(99).shares * 99, 9950, "receivable cannot fund a stock purchase");
    a.rebalance(day + days{2}, 99, 0, 0);
    near(a.value(99).receivable_usd, 50, "selling after ex-date retains entitlement");
    near(a.value(99).shares, 0, "terminal liquidation removes stock exposure");
    near(a.value(99).equity_usd, 10000, "terminal equity retains an unpaid claim at face value");
    a.mark(day + days{2} + hours{20}, 120);
    near(a.value(120).equity_usd, 10000, "post-liquidation prices cannot change terminal cash and claims");
    rejects([&] { a.rebalance(day + days{3}, 99, 1, 0); });
    near(a.value(99).cash_usd, 9950, "a rejected rebalance leaves cash unchanged");
    a.settle_dividends(day + days{3}, 99);
    near(a.value(99).cash_usd, 10000, "payment realizes the receivable in cash");
    a.settle_dividends(day + days{4}, 99);
    near(a.value(99).cash_usd, 10000, "payment cannot be duplicated");
    rejects([&] { a.accrue_dividend(day + days{4}, "d1", day + days{5}, 1, 99); });
    rejects([&] { a.mark(day, 99); });
    trading::LongCashAccount sold{10000};
    sold.rebalance(day, 100, 1, 0);
    sold.rebalance(day + hours{20}, 100, 0, 0);
    sold.accrue_dividend(day + days{1}, "d", day + days{2}, 1, 99);
    near(sold.value(99).receivable_usd, 0, "sale before ex-date earns no entitlement");
    trading::LongCashAccount split{10000};
    split.rebalance(day, 100, 1, 0);
    split.split(day + days{1}, "s1", 2, 100);
    near(split.value(50).shares, 200, "split scales shares");
    near(split.value(50).equity_usd, 10000, "split scales quote units without wealth creation");
    split.accrue_dividend(day + days{1}, "d", day + days{4}, 1, 49);
    split.settle_dividends(day + days{1}, 49);
    rejects([&] { split.split(day + days{1}, "late-split", 2, 49); });
    split.split(day + days{2}, "s2", .25, 49);
    near(split.value(196).shares, 50, "reverse split conserves fractional ownership");
    near(split.value(196).receivable_usd, 200, "later split does not rescale an existing USD claim");
    near(split.value(196).equity_usd, 10000, "split and dividend conserve the constructed wealth");
    rejects([&] { split.split(day + days{3}, "s2", 2, 196); });
    trading::LongCashAccount late{100};
    late.rebalance(day, 10, 1, 0);
    rejects([&] { late.accrue_dividend(day, "late", day + days{1}, 1, 9); });
}

void test_invalid_inputs_and_scale() {
    for (const double invalid : {-1.0, 0.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { trading::LongCashAccount a{invalid}; });
        trading::LongCashAccount a{100};
        rejects([&] { a.mark(day, invalid); });
        rejects([&] { a.split(day, "s", invalid, 100); });
        rejects([&] { a.accrue_dividend(day, "d", day + days{1}, invalid, 100); });
    }
    trading::LongCashAccount a{10000};
    rejects([&] { a.rebalance(day, 100, -1, 0); });
    rejects([&] { a.rebalance(day, 100, 1.1, 0); });
    rejects([&] { a.rebalance(day, 100, .5, -1); });
    rejects([&] { a.rebalance(day, 100, .5, 10000); });
    rejects([&] { a.accrue_dividend(day, "d", day - days{1}, 1, 100); });
    rejects([&] { a.split(day + hours{1}, "s", 2, 100); });
    require(a.ledger().empty(), "rejected events do not enter the ledger");
    for (const double scale : {1e-6, 1.0, 1e6}) {
        trading::LongCashAccount scaled{10000 * scale};
        scaled.rebalance(day, 100, .4, 5);
        near(scaled.value(100).equity_usd / scale, 10000 / 1.0002, "account scales linearly with starting cash");
    }
    trading::LongCashAccount huge{std::numeric_limits<double>::max()};
    huge.rebalance(day, 1, 1, 0);
    rejects([&] { huge.mark(day + days{1}, 2); });
    near(huge.value(1).cash_usd, 0, "overflowing valuation leaves account state unchanged");
    const auto demo = trading::demonstrate_long_cash_account();
    require(demo.checks.size() == 8, "report demonstration verifies eight analytical identities");
}
} // namespace

int main() {
    try {
        test_rebalance_and_overnight();
        test_actions_and_chronology();
        test_invalid_inputs_and_scale();
        std::cout << "Long/cash account contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Long/cash account contract failure: " << error.what() << '\n';
        return 1;
    }
}
