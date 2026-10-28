#include "simulated_broker.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace trading::execution {
namespace {
Cents product(Shares q, Cents p) {
    if (q < 0 || p < 0 || (p && q > std::numeric_limits<Cents>::max() / p)) {
        throw std::invalid_argument("simulated product exceeds integer units");
    }
    return q * p;
}
Cents sum(Cents a, Cents b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Cents>::max() - b) {
        throw std::invalid_argument("simulated sum exceeds integer units");
    }
    return a + b;
}
} // namespace
std::string_view name(BrokerTimeInForce v) {
    switch (v) {
    case BrokerTimeInForce::day:
        return "day";
    case BrokerTimeInForce::ioc:
        return "ioc";
    case BrokerTimeInForce::gtc:
        return "gtc";
    }
    throw std::invalid_argument("unknown broker time in force");
}
SimulatedBroker::SimulatedBroker(std::string symbol, Cents cash, Cents fee)
    : symbol_(std::move(symbol)), initial_cash_(cash), fee_per_share_(fee) {
    if (symbol_.empty() || cash <= 0 || fee < 0) {
        throw std::invalid_argument("invalid simulated broker configuration");
    }
    books_.emplace(symbol_, QuoteBudget{});
}
SimulatedBroker::SimulatedBroker(std::vector<std::string> symbols, Cents cash, Cents fee)
    : initial_cash_(cash), fee_per_share_(fee), portfolio_(true) {
    if (symbols.empty() || symbols.size() > 5 || cash <= 0 || fee < 0) {
        throw std::invalid_argument("invalid portfolio peer configuration");
    }
    for (const auto &s : symbols) {
        if (s.empty() || !books_.emplace(s, QuoteBudget{}).second) {
            throw std::invalid_argument("duplicate or empty peer symbol");
        }
    }
}
void SimulatedBroker::advance_time(std::int64_t now) {
    if (now <= 0 || now < now_) {
        throw std::invalid_argument("simulated broker clock cannot regress");
    }
    now_ = now;
    for (const auto &id : fifo_) {
        auto &o = orders_.at(id);
        if (o.active && o.time_in_force == BrokerTimeInForce::day && o.expires_at_seconds &&
            now >= o.expires_at_seconds) {
            o.active = false;
            report(id, ReportKind::canceled, "", 0, 0, 0, o.filled);
        }
    }
}
void SimulatedBroker::advance_to(std::int64_t now) {
    auto next = *this;
    next.advance_time(now);
    *this = std::move(next);
}
void SimulatedBroker::report(const std::string &id, ReportKind kind, std::string execution, Shares q, Cents p,
                             Cents fee, Shares cumulative, std::string cancel) {
    if (portfolio_ && reports_.size() >= 8192) {
        throw std::invalid_argument("peer activity bound reached");
    }
    reports_.push_back({"SIM-E" + std::to_string(reports_.size() + 1), id, kind, std::move(execution), q, p,
                        fee, cumulative, std::move(cancel)});
}
bool SimulatedBroker::on_quote(const TopOfBook &q) {
    return on_quote(symbol_, q, q.at_seconds);
}
bool SimulatedBroker::on_quote(const std::string &symbol, const TopOfBook &q, std::int64_t receipt) {
    if (!books_.contains(symbol)) {
        throw std::invalid_argument("quote symbol is not registered");
    }
    const auto &book = books_.at(symbol);
    if (const auto old = book.history.find(q.sequence); old != book.history.end()) {
        if (old->second != q) {
            throw std::invalid_argument("conflicting simulated quote sequence");
        }
        if (portfolio_) {
            advance_to(receipt);
        }
        return false;
    }
    std::size_t retained = 0;
    for (const auto &[s, b] : books_) {
        static_cast<void>(s);
        retained += b.history.size();
    }
    if (q.sequence == 0 || q.at_seconds <= 0 || q.at_seconds > receipt || q.bid_cents <= 0 ||
        q.ask_cents < q.bid_cents || q.bid_shares < 0 || q.ask_shares < 0 ||
        (book.quote && q.sequence <= book.quote->sequence) || (portfolio_ && retained >= 4096)) {
        throw std::invalid_argument("invalid, future, out-of-order or excessive quote");
    }
    auto next = *this;
    next.advance_time(receipt);
    auto &updated = next.books_.at(symbol);
    updated.quote = q;
    updated.history.emplace(q.sequence, q);
    updated.bid_remaining = q.bid_shares;
    updated.ask_remaining = q.ask_shares;
    next.match(symbol);
    *this = std::move(next);
    return true;
}
bool SimulatedBroker::submit(const OrderRequest &r, std::int64_t now) {
    return submit(r, now, BrokerTimeInForce::day, 0);
}
bool SimulatedBroker::submit(const OrderRequest &r, std::int64_t now, BrokerTimeInForce tif,
                             std::int64_t expiry) {
    static_cast<void>(name(tif));
    static_cast<void>(name(r.side));
    if (const auto old = orders_.find(r.id); old != orders_.end()) {
        if (old->second.request != r || old->second.time_in_force != tif ||
            old->second.expires_at_seconds != expiry) {
            throw std::invalid_argument("conflicting simulated order ID");
        }
        return false;
    }
    if (r.id.empty() || !books_.contains(r.symbol) || r.shares <= 0 || r.limit_units <= 0 ||
        r.fee_per_share_ceiling_units < 0 || (tif != BrokerTimeInForce::day && expiry != 0) ||
        (portfolio_ && tif == BrokerTimeInForce::day && expiry == 0) || expiry < 0 ||
        (portfolio_ && orders_.size() >= 1024)) {
        throw std::invalid_argument("invalid or excessive simulated order");
    }
    static_cast<void>(product(r.shares, sum(r.limit_units, r.fee_per_share_ceiling_units)));
    auto next = *this;
    next.advance_time(now);
    bool accepted = r.fee_per_share_ceiling_units >= fee_per_share_ && r.limit_units > fee_per_share_ &&
                    (!expiry || expiry > now);
    if (portfolio_) {
        const auto account = next.portfolio_snapshot();
        accepted = accepted && account.open_orders.size() < 32;
        if (r.side == Side::buy) {
            accepted = accepted && product(r.shares, sum(r.limit_units, r.fee_per_share_ceiling_units)) <=
                                       account.cash_units - next.reserved_cash_units();
        } else {
            Shares reserved = 0;
            for (const auto &o : account.open_orders) {
                if (o.request.symbol == r.symbol && o.request.side == Side::sell) {
                    reserved = sum(reserved, o.request.shares - o.filled_shares);
                }
            }
            accepted = accepted && r.shares <= account.positions.at(r.symbol) - reserved;
        }
    }
    next.orders_.emplace(r.id, SimulatedOrderView{r, 0, accepted, accepted, tif, expiry});
    next.fifo_.push_back(r.id);
    next.report(r.id, accepted ? ReportKind::accepted : ReportKind::rejected);
    if (accepted) {
        next.match(r.symbol);
        auto &o = next.orders_.at(r.id);
        if (tif == BrokerTimeInForce::ioc && o.active) {
            o.active = false;
            next.report(r.id, ReportKind::canceled, "", 0, 0, 0, o.filled);
        }
    }
    *this = std::move(next);
    return true;
}
bool SimulatedBroker::cancel(const std::string &id, const std::string &cancel, std::int64_t now) {
    if (const auto old = cancel_ids_.find(cancel); old != cancel_ids_.end()) {
        if (old->second != id) {
            throw std::invalid_argument("conflicting simulated cancellation ID");
        }
        return false;
    }
    if (!orders_.contains(id) || cancel.empty()) {
        throw std::invalid_argument("unknown simulated cancellation");
    }
    auto next = *this;
    next.advance_time(now);
    next.cancel_ids_.emplace(cancel, id);
    next.pending_cancels_.emplace_back(cancel, id);
    *this = std::move(next);
    return true;
}
void SimulatedBroker::complete_cancels(std::int64_t now) {
    auto next = *this;
    next.advance_time(now);
    for (const auto &[cancel, id] : next.pending_cancels_) {
        auto &o = next.orders_.at(id);
        if (o.active) {
            o.active = false;
            next.report(id, ReportKind::canceled, "", 0, 0, 0, o.filled, cancel);
        } else {
            next.report(id, ReportKind::cancel_rejected, "", 0, 0, 0, 0, cancel);
        }
    }
    next.pending_cancels_.clear();
    *this = std::move(next);
}
void SimulatedBroker::match(const std::string &symbol) {
    auto &book = books_.at(symbol);
    if (!book.quote || (portfolio_ && now_ - book.quote->at_seconds > 2)) {
        return;
    }
    for (const auto &id : fifo_) {
        auto &o = orders_.at(id);
        if (!o.active || o.request.symbol != symbol) {
            continue;
        }
        const bool buy = o.request.side == Side::buy;
        const auto price = buy ? book.quote->ask_cents : book.quote->bid_cents;
        if ((buy && price > o.request.limit_units) || (!buy && price < o.request.limit_units)) {
            continue;
        }
        auto &remaining = buy ? book.ask_remaining : book.bid_remaining;
        const auto q = std::min(remaining, o.request.shares - o.filled);
        if (!q) {
            continue;
        }
        const auto fee = product(q, fee_per_share_);
        const auto execution = "SIM-X" + std::to_string(fills_.size() + 1);
        fills_.push_back({execution, id, o.request.side, q, price, fee, book.quote->sequence, symbol});
        o.filled += q;
        remaining -= q;
        if (o.filled == o.request.shares) {
            o.active = false;
        }
        static_cast<void>(portfolio_snapshot());
        report(id, ReportKind::fill, execution, q, price, fee);
    }
}
MoneyUnits SimulatedBroker::reserved_cash_units() const {
    MoneyUnits reserved = 0;
    for (const auto &[id, o] : orders_) {
        static_cast<void>(id);
        if (o.active && o.request.side == Side::buy) {
            reserved =
                sum(reserved, product(o.request.shares - o.filled,
                                      sum(o.request.limit_units, o.request.fee_per_share_ceiling_units)));
        }
    }
    return reserved;
}
PortfolioBrokerSnapshot SimulatedBroker::portfolio_snapshot() const {
    PortfolioBrokerSnapshot a{initial_cash_, {}, {}};
    for (const auto &[s, b] : books_) {
        static_cast<void>(b);
        a.positions.emplace(s, 0);
    }
    for (const auto &f : fills_) {
        const auto gross = product(f.shares, f.price_cents);
        if (f.side == Side::buy) {
            const auto debit = sum(gross, f.fee_units);
            if (debit > a.cash_units) {
                throw std::invalid_argument("simulated broker has insufficient cash");
            }
            a.cash_units -= debit;
            a.positions.at(f.symbol) = sum(a.positions.at(f.symbol), f.shares);
        } else {
            if (f.shares > a.positions.at(f.symbol) || f.fee_units > gross) {
                throw std::invalid_argument("simulated broker cannot sell unavailable shares");
            }
            a.positions.at(f.symbol) -= f.shares;
            a.cash_units = sum(a.cash_units, gross - f.fee_units);
        }
    }
    std::map<std::string, Shares> sells;
    for (const auto &[id, o] : orders_) {
        static_cast<void>(id);
        if (!o.active) {
            continue;
        }
        a.open_orders.push_back({o.request, o.filled});
        if (o.request.side == Side::sell) {
            sells[o.request.symbol] = sum(sells[o.request.symbol], o.request.shares - o.filled);
        }
    }
    if (portfolio_) {
        if (reserved_cash_units() > a.cash_units) {
            throw std::invalid_argument("peer global reservations exceed cash");
        }
        for (const auto &[s, q] : sells) {
            if (q > a.positions.at(s)) {
                throw std::invalid_argument("peer sell reservations exceed symbol inventory");
            }
        }
    }
    return a;
}
BrokerSnapshot SimulatedBroker::snapshot() const {
    if (symbol_.empty()) {
        throw std::logic_error("portfolio broker requires symbol-keyed snapshot");
    }
    const auto a = portfolio_snapshot();
    return {a.cash_units, a.positions.at(symbol_), a.open_orders};
}
std::optional<TopOfBook> SimulatedBroker::quote() const {
    return books_.at(symbol_).quote;
}
const std::map<std::string, QuoteBudget> &SimulatedBroker::quote_budgets() const noexcept {
    return books_;
}
std::vector<SimulatedOrderView> SimulatedBroker::order_views() const {
    std::vector<SimulatedOrderView> out;
    for (const auto &id : fifo_) {
        out.push_back(orders_.at(id));
    }
    return out;
}
const std::vector<BrokerReport> &SimulatedBroker::reports() const noexcept {
    return reports_;
}
const std::vector<SimulatedFill> &SimulatedBroker::fills() const noexcept {
    return fills_;
}
std::size_t SimulatedBroker::order_count() const noexcept {
    return orders_.size();
}
std::int64_t SimulatedBroker::now_seconds() const noexcept {
    return now_;
}
std::string_view SimulatedBroker::symbol() const noexcept {
    return symbol_;
}
} // namespace trading::execution
