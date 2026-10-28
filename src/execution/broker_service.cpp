#include "broker_service.hpp"
#include "market_calendar.hpp"
#include "shared_capital_json.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
const std::vector<std::string> symbols{"GLD", "IWM", "QQQ", "SPY", "TLT"};
const std::string zero(64, '0');
bool token(const std::string &s) {
    return !s.empty() && s.size() <= 96 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
               c == '_';
    });
}
std::int64_t integer(const Json &j) {
    if (!j.is_number_integer() ||
        (j.is_number_unsigned() &&
         j.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
        throw std::invalid_argument("peer requires representable integer units");
    }
    return j.get<std::int64_t>();
}
void fields(const Json &j, const std::set<std::string> &expected) {
    if (!j.is_object()) {
        throw std::invalid_argument("peer command must be an object");
    }
    std::set<std::string> actual;
    for (const auto &[k, v] : j.items()) {
        static_cast<void>(v);
        actual.insert(k);
    }
    if (actual != expected) {
        throw std::invalid_argument("peer command fields differ from contract");
    }
}
Json header(const std::string &account) {
    return {{"kind", "header"},
            {"schema_version", 5},
            {"model_contract", "synthetic_portfolio_broker_v1"},
            {"policy_sha256", TRADING_BROKER_PROTOCOL_SHA256},
            {"account_id", account},
            {"sequence", 0},
            {"previous_sha256", zero}};
}
Json entry(const Json &command, const Json &reply, const BrokerState &state, std::size_t sequence,
           const std::string &previous) {
    return {{"kind", "command"},  {"sequence", sequence}, {"previous_sha256", previous},
            {"command", command}, {"reply", reply},       {"checkpoint", state.snapshot()}};
}
struct Loaded {
    BrokerState state;
    std::size_t records{}, complete{};
    std::string digest, bytes;
};
Loaded load(JournalStorage &storage, const std::string &account) {
    Loaded loaded{BrokerState{account}, 0, 0, "", storage.read_all()};
    std::size_t offset = 0;
    bool first = true;
    while (true) {
        const auto end = loaded.bytes.find('\n', offset);
        if (end == std::string::npos) {
            break;
        }
        const auto [value, hash] =
            JournalFrame::decode(std::string_view{loaded.bytes}.substr(offset, end - offset));
        if (first) {
            if (value != header(account)) {
                throw JournalError("peer journal header, account or policy differs");
            }
            first = false;
        } else {
            if (loaded.records >= 8192) {
                throw JournalError("peer command cap exceeded");
            }
            const auto reply = loaded.state.mutate(value.at("command"));
            if (value != entry(value.at("command"), reply, loaded.state, loaded.records + 1, loaded.digest)) {
                throw JournalError("peer deterministic command replay differs");
            }
            ++loaded.records;
        }
        loaded.digest = hash;
        offset = end + 1;
    }
    if (first || loaded.bytes.size() - offset > maximum_frame_bytes) {
        throw JournalError("peer header missing or tail exceeds frame bound");
    }
    loaded.complete = offset;
    return loaded;
}
} // namespace
BrokerState::BrokerState(std::string account)
    : account_id_(std::move(account)), broker_(symbols, broker_initial_cash_units, broker_fee_units) {
    if (!token(account_id_)) {
        throw std::invalid_argument("invalid synthetic account identity");
    }
}
Json BrokerState::order(const SimulatedOrderView &o) const {
    const auto all = broker_.order_views();
    const auto found =
        std::find_if(all.begin(), all.end(), [&](const auto &v) { return v.request.id == o.request.id; });
    const auto identity = static_cast<std::size_t>(std::distance(all.begin(), found)) + 1;
    return {{"order", broker_order_json(o.request)},
            {"broker_order_id", "B5-O" + std::to_string(identity)},
            {"filled_shares", o.filled},
            {"status", !o.accepted                    ? "rejected"
                       : o.active                     ? "open"
                       : o.filled == o.request.shares ? "filled"
                                                      : "canceled"},
            {"time_in_force", name(o.time_in_force)},
            {"expires_at_seconds", o.expires_at_seconds}};
}
Json BrokerState::snapshot() const {
    const auto account = broker_.portfolio_snapshot();
    Json orders = Json::array(), quotes = Json::array();
    for (const auto &o : broker_.order_views()) {
        if (o.active) {
            orders.push_back(order(o));
        }
    }
    for (const auto &[symbol, book] : broker_.quote_budgets()) {
        if (book.quote) {
            const auto &q = *book.quote;
            quotes.push_back({{"symbol", symbol},
                              {"sequence", q.sequence},
                              {"source_at_seconds", q.at_seconds},
                              {"bid_units", q.bid_cents},
                              {"ask_units", q.ask_cents},
                              {"bid_shares", q.bid_shares},
                              {"ask_shares", q.ask_shares},
                              {"bid_remaining", book.bid_remaining},
                              {"ask_remaining", book.ask_remaining}});
        }
    }
    return {{"account_id", account_id_},
            {"cash_units", account.cash_units},
            {"available_cash_units", account.cash_units},
            {"reserved_cash_units", broker_.reserved_cash_units()},
            {"positions", account.positions},
            {"open_orders", orders},
            {"quotes", quotes},
            {"activity_cursor", activities_.size()},
            {"order_count", broker_.order_count()},
            {"fill_count", broker_.fills().size()},
            {"at_seconds", broker_.now_seconds()},
            {"session_date", session_date_},
            {"session_open_seconds", session_open_},
            {"session_close_seconds", session_close_}};
}
void BrokerState::publish(std::size_t previous) {
    const auto &reports = broker_.reports();
    for (auto i = previous; i < reports.size(); ++i) {
        const auto &r = reports[i];
        const auto all = broker_.order_views();
        const auto found =
            std::find_if(all.begin(), all.end(), [&](const auto &o) { return o.request.id == r.order_id; });
        activities_.push_back({{"cursor", i + 1},
                               {"at_seconds", broker_.now_seconds()},
                               {"broker_order_id", order(*found).at("broker_order_id")},
                               {"report", broker_report_json(r)}});
    }
}
Json BrokerState::apply(const Json &c) {
    const auto kind = c.at("kind").get<std::string>(), id = c.at("command_id").get<std::string>();
    if (!token(id)) {
        throw std::invalid_argument("invalid peer command identity");
    }
    const auto at = integer(c.at("at_seconds"));
    const auto old = broker_.reports().size();
    if (kind == "session") {
        fields(c, {"kind", "command_id", "at_seconds", "date"});
        const auto date = c.at("date").get<std::string>();
        const auto days = extension_calendar({date, date});
        if (days.size() != 1 || date <= session_date_ ||
            at != days[0].open_utc().time_since_epoch().count()) {
            throw std::invalid_argument("peer session differs from chronological exchange calendar");
        }
        session_date_ = date;
        session_open_ = at;
        session_close_ = days[0].close_utc().time_since_epoch().count();
        broker_.advance_to(at);
    } else if (kind == "quote") {
        fields(c, {"kind", "command_id", "at_seconds", "quote"});
        const auto &q = c.at("quote");
        fields(q, {"symbol", "sequence", "source_at_seconds", "bid_units", "ask_units", "bid_shares",
                   "ask_shares"});
        if (at < session_open_ || at >= session_close_) {
            throw std::invalid_argument("quote outside declared open session");
        }
        const auto seq = integer(q.at("sequence"));
        if (seq <= 0) {
            throw std::invalid_argument("nonpositive quote sequence");
        }
        static_cast<void>(broker_.on_quote(
            q.at("symbol").get<std::string>(),
            {static_cast<std::uint64_t>(seq), integer(q.at("source_at_seconds")), integer(q.at("bid_units")),
             integer(q.at("ask_units")), integer(q.at("bid_shares")), integer(q.at("ask_shares"))},
            at));
    } else if (kind == "submit") {
        fields(c, {"kind", "command_id", "at_seconds", "order", "time_in_force"});
        auto r = parse_broker_order(c.at("order"));
        if (!token(r.id)) {
            throw std::invalid_argument("invalid peer order identity");
        }
        const auto tif = c.at("time_in_force").get<std::string>();
        const auto value = tif == "day"   ? BrokerTimeInForce::day
                           : tif == "gtc" ? BrokerTimeInForce::gtc
                           : tif == "ioc" ? BrokerTimeInForce::ioc
                                          : throw std::invalid_argument("unsupported time in force");
        if (at < session_open_ || at >= session_close_) {
            throw std::invalid_argument("submission outside declared open session");
        }
        static_cast<void>(broker_.submit(r, at, value, value == BrokerTimeInForce::day ? session_close_ : 0));
    } else if (kind == "cancel") {
        fields(c, {"kind", "command_id", "at_seconds", "order_id", "cancel_id"});
        const auto cancel = c.at("cancel_id").get<std::string>();
        if (!token(cancel)) {
            throw std::invalid_argument("invalid cancel identity");
        }
        static_cast<void>(broker_.cancel(c.at("order_id").get<std::string>(), cancel, at));
    } else if (kind == "complete_cancels" || kind == "advance") {
        fields(c, {"kind", "command_id", "at_seconds"});
        if (kind == "advance") {
            broker_.advance_to(at);
        } else {
            broker_.complete_cancels(at);
        }
    } else {
        throw std::invalid_argument("unknown mutating peer command");
    }
    publish(old);
    return {{"status", "ok"}, {"activity_cursor", activities_.size()}};
}
Json BrokerState::mutate(const Json &command) {
    const auto id = command.at("command_id").get<std::string>();
    if (const auto old = commands_.find(id); old != commands_.end()) {
        if (old->second.first != command) {
            return {{"status", "denied"}, {"detail", "conflicting durable command identity"}};
        }
        return old->second.second;
    }
    if (commands_.size() >= 8192) {
        throw JournalError("peer unique command limit reached");
    }
    auto candidate = *this;
    Json reply;
    try {
        reply = candidate.apply(command);
    } catch (const Json::exception &e) {
        reply = {{"status", "denied"}, {"detail", std::string{"invalid peer JSON: "} + e.what()}};
        candidate = *this;
    } catch (const std::invalid_argument &e) {
        reply = {{"status", "denied"}, {"detail", e.what()}};
        candidate = *this;
    }
    candidate.commands_.emplace(id, std::make_pair(command, reply));
    *this = std::move(candidate);
    return reply;
}
Json BrokerState::read(const Json &c) const {
    const auto kind = c.at("kind").get<std::string>();
    if (kind == "hello") {
        fields(c, {"kind"});
        return {{"protocol", 1},
                {"account_id", account_id_},
                {"policy_sha256", TRADING_BROKER_PROTOCOL_SHA256},
                {"usd_scale", 1000000000},
                {"fee_per_share_units", broker_fee_units},
                {"initial_cash_units", broker_initial_cash_units},
                {"symbols", symbols}};
    }
    if (kind == "snapshot") {
        fields(c, {"kind"});
        return snapshot();
    }
    if (kind == "lookup") {
        fields(c, {"kind", "order_id"});
        for (const auto &o : broker_.order_views()) {
            if (o.request.id == c.at("order_id").get<std::string>()) {
                return {{"found", true}, {"value", order(o)}};
            }
        }
        return {{"found", false}};
    }
    if (kind == "activities") {
        fields(c, {"kind", "after", "limit"});
        const auto after = integer(c.at("after")), limit = integer(c.at("limit"));
        if (after < 0 || after > static_cast<std::int64_t>(activities_.size()) || limit <= 0 || limit > 16) {
            throw std::invalid_argument("invalid activity cursor or page bound");
        }
        Json page = Json::array();
        const auto end = std::min(activities_.size(), static_cast<std::size_t>(after + limit));
        for (auto i = static_cast<std::size_t>(after); i < end; ++i) {
            page.push_back(activities_[i]);
        }
        return {{"activities", page}, {"next", end}, {"end", activities_.size()}};
    }
    throw std::invalid_argument("unknown peer read command");
}
BrokerService::BrokerService(std::unique_ptr<JournalStorage> storage, BrokerState state, std::size_t records,
                             std::size_t bytes, std::string digest)
    : storage_(std::move(storage)), state_(std::move(state)), records_(records), bytes_(bytes),
      digest_(std::move(digest)) {}
BrokerService BrokerService::create(std::unique_ptr<JournalStorage> storage, std::string account) {
    if (!storage || !storage->read_all().empty()) {
        throw JournalError("new peer journal must be empty");
    }
    auto [bytes, digest] = JournalFrame::encode(header(account));
    storage->append_and_sync(bytes);
    return BrokerService{std::move(storage), BrokerState{std::move(account)}, 0, bytes.size(),
                         std::move(digest)};
}
BrokerService BrokerService::recover(std::unique_ptr<JournalStorage> storage, std::string account) {
    if (!storage) {
        throw JournalError("missing peer storage");
    }
    auto recovered = load(*storage, account);
    if (recovered.complete != recovered.bytes.size()) {
        throw JournalError("peer incomplete tail requires separate verified prefix");
    }
    return BrokerService{std::move(storage), std::move(recovered.state), recovered.records,
                         recovered.complete, std::move(recovered.digest)};
}
BrokerService BrokerService::recover_prefix(std::unique_ptr<JournalStorage> source,
                                            std::unique_ptr<JournalStorage> destination,
                                            std::string account) {
    if (!source || !destination || !destination->read_all().empty()) {
        throw JournalError("prefix recovery requires owned source and empty destination");
    }
    auto recovered = load(*source, account);
    if (recovered.complete == recovered.bytes.size()) {
        throw JournalError("prefix recovery requires incomplete tail");
    }
    destination->append_and_sync(std::string_view{recovered.bytes}.substr(0, recovered.complete));
    return BrokerService{std::move(destination), std::move(recovered.state), recovered.records,
                         recovered.complete, std::move(recovered.digest)};
}
static_assert(std::is_nothrow_move_assignable_v<BrokerState>);
Json BrokerService::exchange(const Json &command) {
    if (!healthy_) {
        throw JournalError("peer writer is uncertain; recover before use");
    }
    const auto kind = command.at("kind").get<std::string>();
    if (kind == "hello" || kind == "snapshot" || kind == "activities" || kind == "lookup") {
        return state_.read(command);
    }
    if (records_ >= 8192) {
        healthy_ = false;
        throw JournalError("peer command record bound reached");
    }
    auto candidate = state_;
    const auto reply = candidate.mutate(command);
    auto [bytes, digest] = JournalFrame::encode(entry(command, reply, candidate, records_ + 1, digest_));
    if (bytes.size() > maximum_journal_bytes - bytes_) {
        healthy_ = false;
        throw JournalError("peer segment byte bound reached");
    }
    try {
        storage_->append_and_sync(bytes);
    } catch (...) {
        healthy_ = false;
        throw;
    }
    state_ = std::move(candidate);
    digest_ = std::move(digest);
    bytes_ += bytes.size();
    ++records_;
    return reply;
}
bool BrokerService::healthy() const noexcept {
    return healthy_;
}
Json BrokerService::snapshot() const {
    if (!healthy_) {
        throw JournalError("peer writer is uncertain");
    }
    return state_.snapshot();
}
} // namespace trading::execution
