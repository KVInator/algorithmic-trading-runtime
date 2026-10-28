#include "shared_capital.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace trading::execution {
namespace {
MoneyUnits add(MoneyUnits a, MoneyUnits b) {
    constexpr auto high = std::numeric_limits<MoneyUnits>::max(), low = std::numeric_limits<MoneyUnits>::min();
    if ((b > 0 && a > high - b) || (b < 0 && a < low - b)) { throw std::invalid_argument("shared unit sum overflow"); }
    return a + b;
}
MoneyUnits product(Shares shares, MoneyUnits price) {
    if (shares < 0 || price < 0 || (price && shares > std::numeric_limits<MoneyUnits>::max() / price)) {
        throw std::invalid_argument("shared notional exceeds integer units");
    }
    return shares * price;
}
bool token(const std::string& s) {
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool terminal(OrderStatus s) { return s == OrderStatus::filled || s == OrderStatus::canceled || s == OrderStatus::rejected; }
// Exact a*b/d and its remainder without a wide integer or floating-point
// conversion. Each partial quotient is bounded by the final signed result.
std::pair<MoneyUnits, MoneyUnits> ratio(MoneyUnits a, MoneyUnits b, MoneyUnits d) {
    if (a < 0 || b < 0 || d <= 0) { throw std::invalid_argument("invalid allocation ratio"); }
    const auto divisor = static_cast<std::uint64_t>(d), whole = static_cast<std::uint64_t>(a / d), part = static_cast<std::uint64_t>(a % d);
    MoneyUnits quotient = 0;
    std::uint64_t remainder = 0;
    for (int bit = 62; bit >= 0; --bit) {
        quotient = add(quotient, quotient);
        MoneyUnits carry = 0;
        if (remainder >= divisor - remainder) { remainder -= divisor - remainder; ++carry; }
        else { remainder += remainder; }
        if ((static_cast<std::uint64_t>(b) >> bit) & 1U) {
            quotient = add(quotient, static_cast<MoneyUnits>(whole));
            if (remainder >= divisor - part) { remainder -= divisor - part; ++carry; }
            else { remainder += part; }
        }
        quotient = add(quotient, carry);
    }
    return {quotient, static_cast<MoneyUnits>(remainder)};
}
std::map<OwnerKey, Shares> allocate(Shares quantity, const std::map<OwnerKey, Shares>& weights) {
    Shares total = 0;
    for (const auto& [key, weight] : weights) { static_cast<void>(key); total = add(total, weight); }
    if (quantity < 0 || total <= 0) { throw std::invalid_argument("allocation requires positive total weight"); }
    std::map<OwnerKey, Shares> result;
    std::vector<std::pair<OwnerKey, Shares>> remainders;
    Shares assigned = 0;
    for (const auto& [key, weight] : weights) {
        const auto [q, r] = ratio(quantity, weight, total);
        result.emplace(key, q); assigned = add(assigned, q); remainders.emplace_back(key, r);
    }
    std::sort(remainders.begin(), remainders.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    const auto left = quantity - assigned;
    if (left < 0 || static_cast<std::size_t>(left) > remainders.size()) { throw std::logic_error("allocation conservation failed"); }
    for (Shares i = 0; i < left; ++i) { ++result.at(remainders[static_cast<std::size_t>(i)].first); }
    return result;
}
}

SharedCapitalConfiguration SharedCapitalConfiguration::registered(const ExtensionStudyContract& contract) {
    const auto& p = contract.document().at("portfolio");
    if (p.at("initial_usd").get<int>() != 100000 || p.at("gross_fraction_limit").get<int>() != 1 ||
        p.at("instrument_target_fraction_limit").get<double>() != .2 || p.at("family_budget_fraction").get<std::string>() != "1/3" ||
        p.at("instrument_within_family_fraction").get<std::string>() != "1/5" || p.at("shorting").get<bool>() || p.at("borrowing").get<bool>()) {
        throw std::invalid_argument("shared-capital registered budget contract changed");
    }
    return {contract.symbols(), p.at("families").get<std::vector<std::string>>(), 100000 * shared_usd_scale};
}
SharedCapitalBook::SharedCapitalBook(SharedCapitalConfiguration config, std::uint64_t generation,
    std::optional<SharedFlatCheckpoint> checkpoint)
    : configuration_(std::move(config)), core_(configuration_.symbols,
        checkpoint ? checkpoint->cash_units : configuration_.initial_cash_units,
        {std::numeric_limits<Shares>::max(), 5}), generation_(generation) {
    auto symbols = configuration_.symbols, families = configuration_.families;
    std::sort(symbols.begin(), symbols.end()); std::sort(families.begin(), families.end());
    if (symbols != std::vector<std::string>{"GLD", "IWM", "QQQ", "SPY", "TLT"} ||
        families != std::vector<std::string>{"momentum", "reversion", "volume_conditioned_continuation"} ||
        configuration_.initial_cash_units <= 0 || configuration_.fee_per_share_ceiling_units < 0 ||
        configuration_.maximum_quote_age_seconds < 0 || configuration_.maximum_quote_age_seconds > 60 ||
        configuration_.dispatch_window_seconds <= 0 || configuration_.dispatch_window_seconds > 300 ||
        generation > 1000000000) { throw std::invalid_argument("invalid shared-capital configuration"); }
    configuration_.symbols = symbols; configuration_.families = families;
    for (const auto& family : families) {
        for (const auto& symbol : symbols) { owners_.emplace(OwnerKey{family, symbol}, OwnerPosition{}); }
    }
    if (checkpoint) {
        if (checkpoint->at_seconds < 0 || checkpoint->attribution_cash_flows.size() != owners_.size()) {
            throw std::invalid_argument("invalid flat checkpoint identities");
        }
        for (const auto& [key, flow] : checkpoint->attribution_cash_flows) {
            if (!owners_.contains(key)) { throw std::invalid_argument("unknown checkpoint owner"); }
            owners_.at(key).attribution_cash_flow_units = flow;
        }
        at_seconds_ = checkpoint->at_seconds;
    }
    core_.disconnect(); validate();
}
const SharedCapitalConfiguration& SharedCapitalBook::configuration() const noexcept { return configuration_; }
MoneyUnits SharedCapitalBook::mark(const std::string& symbol) const {
    const auto& q = quotes_.at(symbol); return q.bid_units + (q.ask_units - q.bid_units) / 2;
}
Shares SharedCapitalBook::desired(const OwnerKey& key) const {
    const auto& owner = owners_.at(key);
    return owner.expires_at_seconds < at_seconds_ ? owner.shares : owner.desired_shares;
}
bool SharedCapitalBook::fresh_quotes() const {
    if (quotes_.size() != configuration_.symbols.size()) { return false; }
    return std::all_of(quotes_.begin(), quotes_.end(), [&](const auto& item) {
        return item.second.source_at_seconds <= at_seconds_ && at_seconds_ - item.second.source_at_seconds <= configuration_.maximum_quote_age_seconds;
    });
}
MoneyUnits SharedCapitalBook::exposure(const std::optional<std::string>& family, const std::optional<std::string>& symbol,
    const std::optional<OwnerKey>& owner) const {
    MoneyUnits total = 0;
    const auto selected = [&](const OwnerKey& key) { return (!family || key.family == *family) && (!symbol || key.symbol == *symbol) && (!owner || key == *owner); };
    for (const auto& [key, position] : owners_) {
        if (selected(key) && position.shares) { total = add(total, product(position.shares, mark(key.symbol))); }
    }
    for (const auto& [id, plan] : plans_) {
        static_cast<void>(id);
        if (plan.request.side == Side::sell) { continue; }
        const auto price = add(std::max(mark(plan.request.symbol), plan.request.limit_units), plan.request.fee_per_share_ceiling_units);
        for (const auto& [key, remaining] : plan.remaining) { if (selected(key)) { total = add(total, product(remaining, price)); } }
    }
    return total;
}
void SharedCapitalBook::validate() const {
    const auto account = core_.portfolio_account();
    auto cash = configuration_.initial_cash_units;
    if (account.open_orders != plans_.size()) { throw std::logic_error("open order lacks a frozen allocation"); }
    std::map<std::string, Shares> owned, sell_claims, buy_claims;
    for (const auto& symbol : configuration_.symbols) { owned[symbol] = sell_claims[symbol] = buy_claims[symbol] = 0; }
    for (const auto& [key, owner] : owners_) {
        if (owner.shares < 0 || owner.desired_shares < 0) { throw std::logic_error("negative ownership or target"); }
        owned.at(key.symbol) = add(owned.at(key.symbol), owner.shares);
        cash = add(cash, owner.attribution_cash_flow_units);
    }
    if (cash != account.cash_units) { throw std::logic_error("owner cash flows do not reconcile to global cash"); }
    for (const auto& [id, plan] : plans_) {
        const auto order = core_.order(id);
        Shares total = 0;
        for (const auto& [key, q] : plan.remaining) {
            if (key.symbol != plan.request.symbol || q < 0 || !owners_.contains(key)) { throw std::logic_error("invalid frozen ownership claim"); }
            total = add(total, q);
            if (plan.request.side == Side::sell) {
                if (q > owners_.at(key).shares) { throw std::logic_error("sell claim exceeds owner inventory"); }
                sell_claims.at(key.symbol) = add(sell_claims.at(key.symbol), q);
            } else { buy_claims.at(key.symbol) = add(buy_claims.at(key.symbol), q); }
        }
        if (terminal(order.status) || order.request != plan.request || total != order.request.shares - order.filled_shares) {
            throw std::logic_error("frozen owner claims differ from the broker order");
        }
    }
    for (const auto& [symbol, position] : account.positions) {
        if (owned.at(symbol) != position.shares || sell_claims.at(symbol) != position.reserved_sell_shares || buy_claims.at(symbol) != position.outstanding_buy_shares) {
            throw std::logic_error("symbol inventory or reservations do not reconcile to owners");
        }
    }
}
SharedCapitalSnapshot SharedCapitalBook::snapshot() const {
    const auto account = core_.portfolio_account();
    auto equity = account.cash_units;
    for (const auto& [symbol, position] : account.positions) { if (position.shares) { equity = add(equity, product(position.shares, mark(symbol))); } }
    std::vector<std::string> breaches;
    if (!quotes_.empty()) {
        for (const auto& [key, owner] : owners_) {
            static_cast<void>(owner);
            if (exposure({}, {}, key) > equity / 15) { breaches.push_back("owner:" + key.family + '/' + key.symbol); }
        }
        for (const auto& family : configuration_.families) { if (exposure(family, {}) > equity / 3) { breaches.push_back("family:" + family); } }
        for (const auto& symbol : configuration_.symbols) { if (exposure({}, symbol) > equity / 5) { breaches.push_back("symbol:" + symbol); } }
        if (exposure({}, {}) > equity) { breaches.push_back("gross"); }
    }
    std::vector<OrderView> open;
    for (const auto& order : core_.orders()) { if (!terminal(order.status)) { open.push_back(order); } }
    return {account, owners_, quotes_, plans_, std::move(open), equity, std::move(breaches), at_seconds_, generation_, sealed_};
}
SharedFlatCheckpoint SharedCapitalBook::flat_checkpoint() const {
    const auto account = core_.portfolio_account();
    if (account.open_orders != 0 || std::any_of(owners_.begin(), owners_.end(), [](const auto& pair) { return pair.second.shares || pair.second.desired_shares; })) {
        throw std::invalid_argument("segment handover requires flat inventory, cash targets and no open orders");
    }
    std::map<OwnerKey, MoneyUnits> flows;
    for (const auto& [key, owner] : owners_) { flows.emplace(key, owner.attribution_cash_flow_units); }
    return {account.cash_units, std::move(flows), at_seconds_};
}
SharedResult SharedCapitalBook::apply(const SharedInput& input) {
    if (sealed_) { return {Outcome::denied, "sealed segment cannot accept further commands"}; }
    if (const auto* batch = std::get_if<SharedTargetBatch>(&input)) {
        if (const auto old = batches_.find(batch->id); old != batches_.end()) {
            auto canonical = *batch;
            std::sort(canonical.quotes.begin(), canonical.quotes.end(), [](const auto& a, const auto& b) { return a.symbol < b.symbol; });
            std::sort(canonical.signals.begin(), canonical.signals.end(), [](const auto& a, const auto& b) { return a.owner < b.owner; });
            if (canonical == old->second) { return {Outcome::duplicate, "identical target batch already applied"}; }
            core_.disconnect(); return {Outcome::quarantined, "conflicting target batch identity"};
        }
    }
    const auto at = std::visit([](const auto& item) { return item.at_seconds; }, input);
    try {
        if (at <= 0 || at < at_seconds_) { throw std::invalid_argument("receipt/decision clock regressed"); }
        auto candidate = *this; candidate.at_seconds_ = at;
        auto result = std::visit([&](const auto& item) { return candidate.handle(item); }, input);
        candidate.validate();
        *this = std::move(candidate); return result;
    } catch (const std::invalid_argument& error) {
        if (std::holds_alternative<SharedDelivery>(input) || std::holds_alternative<SharedReconciliation>(input)) {
            core_.disconnect(); return {Outcome::quarantined, error.what()};
        }
        return {Outcome::denied, error.what()};
    }
}
SharedResult SharedCapitalBook::handle(const SharedConnection& input) {
    if (input.connected) { core_.reconnect(); } else { core_.disconnect(); }
    return {Outcome::applied, "connection changed; routing requires exact reconciliation"};
}
SharedResult SharedCapitalBook::handle(const SharedReconciliation& input) {
    const auto outcome = core_.reconcile(input.broker);
    return {outcome, outcome == Outcome::applied ? "global cash, all positions and complete open orders reconcile" : "broker snapshot differs"};
}
SharedResult SharedCapitalBook::handle(const SharedSeal&) {
    static_cast<void>(flat_checkpoint());
    core_.disconnect(); sealed_ = true;
    return {Outcome::applied, "flat segment sealed before successor publication"};
}
SharedResult SharedCapitalBook::handle(const SharedTargetBatch& input) {
    auto canonical = input;
    std::sort(canonical.quotes.begin(), canonical.quotes.end(), [](const auto& a, const auto& b) { return a.symbol < b.symbol; });
    std::sort(canonical.signals.begin(), canonical.signals.end(), [](const auto& a, const auto& b) { return a.owner < b.owner; });
    if (!token(input.id) || input.quotes.size() != configuration_.symbols.size() || input.signals.size() > owners_.size() || batches_.size() >= 10000) {
        throw std::invalid_argument("target batch identity or bounded universe differs");
    }
    std::map<std::string, SharedQuote> quotes;
    for (const auto& quote : canonical.quotes) {
        if (!core_.portfolio_account().positions.contains(quote.symbol) || quote.bid_units <= configuration_.fee_per_share_ceiling_units ||
            quote.ask_units < quote.bid_units || quote.source_at_seconds <= 0 || quote.source_at_seconds > at_seconds_ ||
            at_seconds_ - quote.source_at_seconds > configuration_.maximum_quote_age_seconds || !quotes.emplace(quote.symbol, quote).second) {
            throw std::invalid_argument("target batch requires all five causal fresh quotes");
        }
    }
    quotes_ = std::move(quotes);
    const auto equity = snapshot().equity_units;
    std::set<OwnerKey> seen;
    for (const auto& signal : canonical.signals) {
        const auto found = owners_.find(signal.owner);
        if (found == owners_.end() || !seen.insert(signal.owner).second || at_seconds_ <= found->second.decision_at_seconds ||
            at_seconds_ > std::numeric_limits<std::int64_t>::max() - configuration_.dispatch_window_seconds) {
            throw std::invalid_argument("owner target must be unique, registered and strictly chronological");
        }
        auto& owner = found->second;
        owner.desired_shares = signal.long_target ? (equity / 15) / add(quotes_.at(signal.owner.symbol).ask_units, configuration_.fee_per_share_ceiling_units) : 0;
        owner.decision_at_seconds = at_seconds_;
        owner.expires_at_seconds = at_seconds_ + configuration_.dispatch_window_seconds;
    }
    batches_.emplace(input.id, canonical);
    SharedResult result{Outcome::applied, "shared targets sized and routed from current equity"};
    route(result); return result;
}
SharedResult SharedCapitalBook::handle(const SharedDelivery& input) {
    const auto outcome = core_.receive(input.report);
    SharedResult result{outcome, "broker delivery processed by the global integer core"};
    if (outcome == Outcome::applied && input.report.kind == ReportKind::fill) {
        auto& plan = plans_.at(input.report.order_id);
        const auto quantities = allocate(input.report.fill_shares, plan.remaining);
        const auto fees = allocate(input.report.fee_units, quantities);
        for (const auto& [key, quantity] : quantities) {
            if (!quantity) { continue; }
            auto& owner = owners_.at(key);
            const auto cash = product(quantity, input.report.fill_price_units);
            owner.shares = plan.request.side == Side::buy ? add(owner.shares, quantity) : owner.shares - quantity;
            owner.attribution_cash_flow_units = add(owner.attribution_cash_flow_units,
                plan.request.side == Side::buy ? -add(cash, fees.at(key)) : cash - fees.at(key));
            plan.remaining.at(key) -= quantity;
            result.fills.push_back({key, quantity, fees.at(key)});
        }
    }
    if (outcome == Outcome::applied || outcome == Outcome::obsolete) {
        if (plans_.contains(input.report.order_id) && terminal(core_.order(input.report.order_id).status)) { plans_.erase(input.report.order_id); }
        route(result);
    }
    return result;
}

void SharedCapitalBook::route(SharedResult& result) {
    const auto account = core_.portfolio_account();
    if (!account.connected || account.reconciliation_required) { return; }
    // A new target never changes the owner claims of an order already sent.
    for (const auto& [id, plan] : plans_) {
        bool changed = false;
        for (const auto& [key, owner] : owners_) {
            if (key.symbol != plan.request.symbol) { continue; }
            const auto found = plan.remaining.find(key);
            const auto remaining = found == plan.remaining.end() ? 0 : found->second;
            const auto projected = plan.request.side == Side::buy ? add(owner.shares, remaining) : owner.shares - remaining;
            if (projected != desired(key)) { changed = true; }
        }
        if (plan.request.side == Side::buy) {
            const auto equity = snapshot().equity_units;
            if (exposure({}, {}) > equity || exposure({}, plan.request.symbol) > equity / 5) { changed = true; }
            for (const auto& [key, quantity] : plan.remaining) {
                if (quantity && (exposure({}, {}, key) > equity / 15 || exposure(key.family, {}) > equity / 3)) { changed = true; }
            }
        }
        if (changed && core_.order(id).status != OrderStatus::pending_cancel) {
            const auto cancel_id = id + "-c" + std::to_string(++next_cancel_);
            if (core_.request_cancel(id, cancel_id) != Outcome::applied) { throw std::logic_error("constructed cancel failed core validation"); }
            result.dispatch_cancels.emplace_back(id, cancel_id);
        }
    }
    if (!fresh_quotes()) { return; }
    std::set<std::string> pending;
    for (const auto& [id, plan] : plans_) { static_cast<void>(id); pending.insert(plan.request.symbol); }
    const auto submit = [&](const std::string& symbol, Side side, const std::map<OwnerKey, Shares>& claims) {
        Shares total = 0; for (const auto& [key, q] : claims) { static_cast<void>(key); total = add(total, q); }
        if (!total) { return; }
        const auto& quote = quotes_.at(symbol);
        const auto id = "S4-" + std::to_string(generation_) + '-' + std::to_string(++next_order_);
        OrderRequest order{id, symbol, side, total, side == Side::buy ? quote.ask_units : quote.bid_units, configuration_.fee_per_share_ceiling_units};
        if (core_.submit(order) != Outcome::applied) { throw std::logic_error("shared admission differs from integer core reservations"); }
        plans_.emplace(id, SharedOrderPlan{order, claims}); result.dispatch_orders.push_back(order); pending.insert(symbol);
    };
    // Ownership transfers use free inventory only and preserve broker cash and
    // inventory. Their marked attribution value is explicit in the result.
    for (const auto& symbol : configuration_.symbols) {
        if (pending.contains(symbol)) { continue; }
        for (auto& [seller, position] : owners_) {
            if (seller.symbol != symbol || position.shares <= desired(seller)) { continue; }
            for (auto& [buyer, receiving] : owners_) {
                if (buyer.symbol != symbol || receiving.shares >= desired(buyer)) { continue; }
                const auto equity = snapshot().equity_units, price = mark(symbol);
                const auto owner_free = std::max<MoneyUnits>(0, equity / 15 - exposure({}, {}, buyer));
                const auto family_free = std::max<MoneyUnits>(0, equity / 3 - exposure(buyer.family, {}));
                const auto q = std::min({position.shares - desired(seller), desired(buyer) - receiving.shares, owner_free / price, family_free / price});
                if (!q) { continue; }
                const auto value = product(q, price);
                position.shares -= q; receiving.shares = add(receiving.shares, q);
                position.attribution_cash_flow_units = add(position.attribution_cash_flow_units, value);
                receiving.attribution_cash_flow_units = add(receiving.attribution_cash_flow_units, -value);
                result.transfers.push_back({seller, buyer, q, price});
            }
        }
        std::map<OwnerKey, Shares> sells;
        for (const auto& [key, owner] : owners_) { if (key.symbol == symbol && owner.shares > desired(key)) { sells.emplace(key, owner.shares - desired(key)); } }
        submit(symbol, Side::sell, sells);
    }
    for (const auto& symbol : configuration_.symbols) {
        if (pending.contains(symbol)) { continue; }
        const auto state = snapshot();
        const auto& quote = quotes_.at(symbol);
        const auto cost = add(quote.ask_units, configuration_.fee_per_share_ceiling_units);
        const auto risk_cost = add(std::max(mark(symbol), quote.ask_units), configuration_.fee_per_share_ceiling_units);
        const auto symbol_free = std::max<MoneyUnits>(0, state.equity_units / 5 - exposure({}, symbol));
        const auto gross_free = std::max<MoneyUnits>(0, state.equity_units - exposure({}, {}));
        const auto affordable = std::min({(state.account.cash_units - state.account.reserved_cash_units) / cost, symbol_free / risk_cost, gross_free / risk_cost});
        std::map<OwnerKey, Shares> requests;
        Shares total = 0;
        for (const auto& [key, owner] : owners_) {
            if (key.symbol != symbol || owner.shares >= desired(key)) { continue; }
            const auto owner_free = std::max<MoneyUnits>(0, state.equity_units / 15 - exposure({}, {}, key));
            const auto family_free = std::max<MoneyUnits>(0, state.equity_units / 3 - exposure(key.family, {}));
            const auto q = std::min({desired(key) - owner.shares, owner_free / risk_cost, family_free / risk_cost});
            if (q) { requests.emplace(key, q); total = add(total, q); }
        }
        if (total && affordable) { submit(symbol, Side::buy, allocate(std::min(total, affordable), requests)); }
    }
}
} // namespace trading::execution
