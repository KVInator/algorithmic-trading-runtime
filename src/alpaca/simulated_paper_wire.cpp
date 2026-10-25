#include "simulated_paper_wire.hpp"
#include "utc_timestamp.hpp"
#include <iomanip>
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string decimal(std::int64_t nanos) {
    if (nanos < 0)
        throw std::invalid_argument("negative simulated USD value");
    std::ostringstream out;
    out << nanos / 1000000000 << '.' << std::setw(9) << std::setfill('0') << nanos % 1000000000;
    return out.str();
}
std::string dollars(execution::Cents cents) {
    if (cents < 0 || cents > std::numeric_limits<std::int64_t>::max() / 10000000)
        throw std::invalid_argument("simulated USD conversion exceeds nanodollar range");
    return decimal(cents * 10000000);
}
std::string uuid(std::size_t sequence) {
    std::ostringstream out;
    out << "00000000-0000-4000-8000-" << std::setw(12) << std::setfill('0') << std::hex << sequence;
    return out.str();
}
} // namespace
SimulatedPaperWire::SimulatedPaperWire(std::string account_id, execution::Cents cash)
    : account_id_{std::move(account_id)}, broker_{"SPY", cash, 0} {
    if (cash > 100000000)
        throw std::invalid_argument("simulated paper cash exceeds USD 1 million");
    static_cast<void>(AlpacaPaperDecoder{}.account(account().dump()));
}
std::string SimulatedPaperWire::timestamp() const {
    const auto base = parse_utc_seconds("2023-01-03T14:30:00Z");
    return format_utc_seconds(base + std::chrono::seconds{broker_.now_seconds()});
}
Json SimulatedPaperWire::account() const {
    return {{"id", account_id_},        {"cash", dollars(broker_.snapshot().cash_units)},
            {"currency", "USD"},        {"status", "ACTIVE"},
            {"trading_blocked", false}, {"account_blocked", false}};
}
Json SimulatedPaperWire::positions() const {
    const auto position = broker_.snapshot().position_shares;
    if (position == 0)
        return Json::array();
    // Descriptive moving-average cost, truncated at 1e-9 USD. It is never used
    // to infer executions or reconcile cash; the fill ledger owns those values.
    std::int64_t basis = 0;
    execution::Shares quantity = 0;
    for (const auto &fill : broker_.fills()) {
        if (fill.side == execution::Side::buy) {
            basis += fill.shares * fill.price_cents * 10000000;
            quantity += fill.shares;
        } else {
            basis -= (basis / quantity) * fill.shares + (basis % quantity) * fill.shares / quantity;
            quantity -= fill.shares;
        }
    }
    return Json::array({{{"symbol", "SPY"},
                         {"qty", std::to_string(position)},
                         {"avg_entry_price", decimal(basis / position)},
                         {"side", "long"},
                         {"asset_class", "us_equity"}}});
}
void SimulatedPaperWire::publish() {
    const auto &reports = broker_.reports();
    for (; consumed_reports_ < reports.size(); ++consumed_reports_) {
        const auto &report = reports[consumed_reports_];
        auto &order = orders_.at(report.order_id);
        std::string event;
        Json data;
        if (report.kind == execution::ReportKind::accepted) {
            event = "new";
            order["status"] = "new";
        } else if (report.kind == execution::ReportKind::fill) {
            const auto cumulative =
                BrokerDecimal{order.at("filled_qty").get<std::string>()}.scaled_integer(0) +
                report.fill_shares;
            const auto qty = BrokerDecimal{order.at("qty").get<std::string>()}.scaled_integer(0);
            std::int64_t gross = 0;
            for (const auto &fill : broker_.fills())
                if (fill.order_id == report.order_id) {
                    gross += fill.shares * fill.price_cents;
                    if (fill.execution_id == report.execution_id)
                        break;
                }
            order["filled_qty"] = std::to_string(cumulative);
            order["filled_avg_price"] = decimal(gross * 10000000 / cumulative);
            event = cumulative == qty ? "fill" : "partial_fill";
            order["status"] = cumulative == qty ? "filled" : "partially_filled";
            reported_position_ += order.at("side") == "buy" ? report.fill_shares : -report.fill_shares;
            // Synthetic stable UUIDs are deliberately distinct from client IDs.
            data = {{"execution_id", uuid(100000 + consumed_reports_)},
                    {"qty", std::to_string(report.fill_shares)},
                    {"price", dollars(report.fill_price_units)},
                    {"position_qty", std::to_string(reported_position_)}};
        } else if (report.kind == execution::ReportKind::canceled) {
            event = "canceled";
            order["status"] = event;
        } else if (report.kind == execution::ReportKind::cancel_rejected) {
            event = "order_cancel_rejected";
        } else {
            event = "rejected";
            order["status"] = event;
        }
        order["updated_at"] = timestamp();
        data["event"] = event;
        data["order"] = order;
        data["timestamp"] = timestamp();
        const auto raw = Json{{"stream", "trade_updates"}, {"data", data}}.dump();
        history_.push_back(raw);
        if (opened_ && subscribed_)
            messages_.push_back(raw);
    }
}
PaperHttpResponse SimulatedPaperWire::request(const PaperRequest &request) {
    const auto path = validate_paper_request(request);
    if (request.method == "GET") {
        if (path == "/v2/account") {
            auto value = account();
            return {200, value.dump(), "", "synthetic-account"};
        }
        if (path == "/v2/positions")
            return {200, positions().dump(), "", "synthetic-positions"};
        if (path.starts_with("/v2/orders?")) {
            Json open = Json::array();
            for (const auto &order : broker_.snapshot().open_orders)
                open.push_back(orders_.at(order.request.id));
            return {200, open.dump(), "", "synthetic-open-orders"};
        }
        const auto id = path.substr(path.find('=') + 1);
        const auto order = orders_.find(id);
        return order == orders_.end() ? PaperHttpResponse{404, "{}", "", "synthetic-missing"}
                                      : PaperHttpResponse{200, order->second.dump(), "", "synthetic-lookup"};
    }
    if (request.method == "POST") {
        auto order = Json::parse(request.body);
        const auto id = order.at("client_order_id").get<std::string>();
        if (orders_.contains(id))
            return {422, R"({"message":"duplicate client identity"})", "", "synthetic-duplicate"};
        const execution::OrderRequest intent{
            id,
            "SPY",
            order.at("side") == "buy" ? execution::Side::buy : execution::Side::sell,
            BrokerDecimal{order.at("qty").get<std::string>()}.scaled_integer(0),
            BrokerDecimal{order.at("limit_price").get<std::string>()}.scaled_integer(2),
            0};
        if (intent.shares > 1000 || intent.limit_units > 1000000)
            throw std::invalid_argument("simulated paper order exceeds fixture bounds");
        order["id"] = uuid(submissions_ + 1);
        order["asset_class"] = "us_equity";
        order["status"] = "accepted";
        order["filled_qty"] = "0";
        order["filled_avg_price"] = nullptr;
        order["updated_at"] = timestamp();
        static_cast<void>(broker_.submit(intent, std::max<std::int64_t>(1, broker_.now_seconds())));
        orders_.emplace(id, order);
        ++submissions_;
        publish();
        if (lose_response_) {
            lose_response_ = false;
            throw std::runtime_error("synthetic response lost after broker acceptance");
        }
        return {200, orders_.at(id).dump(), "", "synthetic-submit"};
    }
    const auto broker_id = path.substr(11);
    for (const auto &[id, order] : orders_)
        if (order.at("id") == broker_id) {
            static_cast<void>(broker_.cancel(id, "SIM-C-" + std::to_string(consumed_reports_),
                                             std::max<std::int64_t>(1, broker_.now_seconds())));
            return {204, "", "", "synthetic-cancel-request"};
        }
    return {404, "{}", "", "synthetic-missing"};
}
void SimulatedPaperWire::open_authenticated_stream() {
    opened_ = true;
    messages_.push_back(R"({"stream":"authorization","data":{"status":"authorized"}})");
}
void SimulatedPaperWire::subscribe_trade_updates() {
    if (!opened_)
        throw std::logic_error("simulated stream is closed");
    subscribed_ = true;
    messages_.push_back(R"({"stream":"listening","data":{"streams":["trade_updates"]}})");
}
void SimulatedPaperWire::ping(std::string_view token, std::chrono::milliseconds) {
    if (!opened_)
        throw std::logic_error("simulated stream is closed");
    pong_ = std::string{token};
}
std::optional<std::string> SimulatedPaperWire::take_pong() {
    auto out = std::move(pong_);
    pong_.reset();
    return out;
}
std::optional<std::string> SimulatedPaperWire::poll(std::chrono::milliseconds) {
    if (!opened_)
        throw std::runtime_error("simulated peer disconnected");
    if (messages_.empty())
        return std::nullopt;
    auto out = std::move(messages_.front());
    messages_.pop_front();
    return out;
}
void SimulatedPaperWire::close() noexcept {
    opened_ = false;
    subscribed_ = false;
    messages_.clear();
    pong_.reset();
}
void SimulatedPaperWire::advance_to(std::int64_t source_seconds) { broker_.advance_to(source_seconds); }
void SimulatedPaperWire::on_quote(const execution::TopOfBook &quote) {
    if (quote.bid_cents > 1000000 || quote.ask_cents > 1000000 || quote.bid_shares > 1000 ||
        quote.ask_shares > 1000)
        throw std::invalid_argument("simulated quote exceeds fixture bounds");
    if (broker_.on_quote(quote))
        quotes_.push_back({{"sequence", quote.sequence},
                           {"at_seconds", quote.at_seconds},
                           {"bid_cents", quote.bid_cents},
                           {"ask_cents", quote.ask_cents},
                           {"bid_shares", quote.bid_shares},
                           {"ask_shares", quote.ask_shares}});
    publish();
}
void SimulatedPaperWire::complete_cancels() {
    broker_.complete_cancels(std::max<std::int64_t>(1, broker_.now_seconds()));
    publish();
}
void SimulatedPaperWire::lose_next_submit_response() noexcept { lose_response_ = true; }
void SimulatedPaperWire::duplicate_last_update() {
    if (!subscribed_ || history_.empty())
        throw std::logic_error("no simulated update available for redelivery");
    messages_.push_back(history_.back());
}
void SimulatedPaperWire::replay_trade_history() {
    if (!subscribed_)
        throw std::logic_error("history replay requires subscription");
    for (const auto &raw : history_)
        messages_.push_back(raw);
}
Json SimulatedPaperWire::snapshot() const {
    Json orders = Json::array();
    for (const auto &[id, order] : orders_) {
        static_cast<void>(id);
        orders.push_back(order);
    }
    Json fills = Json::array();
    for (const auto &f : broker_.fills())
        fills.push_back({{"order_id", f.order_id},
                         {"quantity", f.shares},
                         {"side", execution::name(f.side)},
                         {"price_cents", f.price_cents},
                         {"quote_sequence", f.quote_sequence}});
    return {{"account", account()},        {"positions", positions()}, {"orders", orders},
            {"submissions", submissions_}, {"quotes", quotes_},        {"fills", fills}};
}
const std::vector<std::string> &SimulatedPaperWire::trade_history() const noexcept { return history_; }
std::size_t SimulatedPaperWire::submissions() const noexcept { return submissions_; }
} // namespace trading
