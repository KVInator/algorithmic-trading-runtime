#include "paper_transport.hpp"
#include "content_digest.hpp"
#include "journal_frame.hpp"
#include "json_input.hpp"
#include "runtime_json.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace trading {
namespace {
using Json = nlohmann::json;

void require(bool value, std::string_view message) {
    if (!value)
        throw std::invalid_argument(std::string{message});
}

Json projection(const PaperHttpResponse &a, const PaperHttpResponse &p, const PaperHttpResponse &o) {
    const AlpacaPaperDecoder decoder{};
    const auto account = decoder.account(a.body);
    Json positions = Json::array(), orders = Json::array();
    for (const auto &value : decoder.positions(p.body))
        positions.push_back({value.symbol, value.quantity.text(), value.average_entry_price.text()});
    for (const auto &value : decoder.open_orders(o.body))
        orders.push_back({value.broker_id, value.client_id, value.symbol, execution::name(value.side),
                          value.quantity.text(), value.filled_quantity.text(), value.limit_price.text(),
                          value.status});
    std::sort(orders.begin(), orders.end());
    return {{"id", account.id},         {"cash", account.cash.text()},
            {"status", account.status}, {"blocked", account.account_blocked || account.trading_blocked},
            {"positions", positions},   {"orders", orders}};
}
} // namespace

std::string validate_paper_request(const PaperRequest &r) {
    const AlpacaPaperRequests factory{};
    constexpr std::string_view base = "https://paper-api.alpaca.markets";
    require(r.url.starts_with(std::string{base} + "/"), "paper transport refuses non-paper origin");
    const auto path = r.url.substr(base.size());
    PaperRequest expected;
    if (r.method == "GET") {
        require(r.body.empty(), "GET body is unsupported");
        if (path == "/v2/account")
            expected = factory.account();
        else if (path == "/v2/positions")
            expected = factory.positions();
        else if (path == "/v2/orders?status=open&limit=500&direction=asc&nested=false")
            expected = factory.open_orders();
        else {
            constexpr std::string_view prefix = "/v2/orders:by_client_order_id?client_order_id=";
            require(path.starts_with(prefix), "unsupported paper GET path");
            expected = factory.lookup(path.substr(prefix.size()));
        }
    } else if (r.method == "DELETE") {
        require(r.body.empty() && path.starts_with("/v2/orders/"), "unsupported paper DELETE");
        expected = factory.cancel(path.substr(11));
    } else if (r.method == "POST") {
        require(path == "/v2/orders" && r.body.size() < 4096, "unsupported paper POST");
        const auto body = parse_json_document(r.body);
        try {
            const auto side = body.at("side").get<std::string>();
            require(side == "buy" || side == "sell", "unsupported order side");
            const execution::OrderRequest order{
                body.at("client_order_id").get<std::string>(),
                body.at("symbol").get<std::string>(),
                side == "buy" ? execution::Side::buy : execution::Side::sell,
                BrokerDecimal{body.at("qty").get<std::string>()}.scaled_integer(0),
                BrokerDecimal{body.at("limit_price").get<std::string>()}.scaled_integer(2),
                0};
            expected = factory.submit(order);
            require(body == parse_json_document(expected.body),
                    "paper POST differs from supported canonical order fields");
        } catch (const Json::exception &) {
            throw std::invalid_argument("invalid paper POST data types");
        }
    } else
        throw std::invalid_argument("unsupported paper HTTP method");
    require(r.url == expected.url, "noncanonical paper request URL");
    return path;
}

std::optional<std::string> PaperMessageAssembler::push(std::string_view bytes, bool binary, bool final) {
    if ((binary_ && *binary_ != binary) || pending_.size() + bytes.size() > 1024U * 1024U ||
        ++fragments_ > 4096) {
        reset();
        throw std::invalid_argument("invalid, mixed or oversized WebSocket message");
    }
    binary_ = binary;
    pending_.append(bytes);
    if (!final)
        return std::nullopt;
    auto result = std::move(pending_);
    reset();
    require(!result.empty(), "empty paper stream message");
    return result;
}

void PaperMessageAssembler::reset() noexcept {
    pending_.clear();
    binary_.reset();
    fragments_ = 0;
}

PaperCapture::PaperCapture(std::filesystem::path directory) : directory_{std::move(directory)} {
    std::filesystem::create_directories(directory_);
    index_ = execution::create_journal_file(directory_ / "capture.journal");
}

void PaperCapture::record(std::string kind, std::string detail, std::string_view body,
                          const PaperReceipt &receipt, unsigned status, std::string request_id,
                          std::string retry_after) {
    require(body.size() <= 1024U * 1024U && detail.size() <= 4096 && request_id.size() <= 256 &&
                retry_after.size() <= 256,
            "capture record exceeds bounds");
    require(records_ < 1000 && bytes_ + body.size() <= 32U * 1024U * 1024U, "capture segment exhausted");
    static_cast<void>(BrokerTimestamp{receipt.utc});
    const auto file = "body_" + std::to_string(records_) + ".txt";
    auto storage = execution::create_journal_file(directory_ / file);
    storage->append_and_sync(body);
    storage.reset();
    const Json record{{"sequence", records_},
                      {"previous_sha256", previous_},
                      {"kind", std::move(kind)},
                      {"detail", std::move(detail)},
                      {"body_file", file},
                      {"body_sha256", sha256(body)},
                      {"received_at", receipt.utc},
                      {"monotonic_ms", receipt.monotonic_ms},
                      {"status", status},
                      {"request_id", std::move(request_id)},
                      {"retry_after", std::move(retry_after)}};
    auto [frame, digest] = execution::JournalFrame::encode(record);
    require(index_bytes_ + frame.size() <= 3U * 1024U * 1024U, "capture index exhausted");
    index_->append_and_sync(frame);
    index_bytes_ += frame.size();
    previous_ = std::move(digest);
    ++records_;
    bytes_ += body.size();
}

std::size_t PaperCapture::records() const noexcept { return records_; }

PaperSession::PaperSession(execution::JournaledPaperRuntime &r, PaperWire &w, PaperCapture &c, Clock clock,
                           Wait wait)
    : runtime_{r}, wire_{w}, capture_{c}, clock_{clock}, http_{w, c, std::move(clock), std::move(wait)} {
    require(static_cast<bool>(clock_), "transport clock is required");
}

void PaperSession::fault() noexcept {
    active_ = false;
    wire_.close();
    try {
        if (runtime_.healthy())
            static_cast<void>(runtime_.disconnect());
    } catch (...) {
    }
}

void PaperSession::disconnect() {
    active_ = false;
    wire_.close();
    static_cast<void>(runtime_.disconnect());
    capture_.record("connection", "closed", "", clock_());
}

void PaperSession::ingest(const std::string &message, const PaperReceipt &receipt) {
    const auto result = runtime_.receive(message, receipt.utc);
    require(result.outcome != execution::Outcome::quarantined,
            "paper bridge quarantined inbound stream evidence");
    const auto decoded = AlpacaPaperDecoder{}.stream(message, BrokerTimestamp{receipt.utc});
    if (decoded.kind == PaperStreamKind::trade_update)
        ++trades_;
}

void PaperSession::connect() {
    try {
        disconnect();
        wire_.open_authenticated_stream();
        auto auth = wire_.poll(std::chrono::seconds{10});
        require(auth.has_value(), "paper authentication response deadline");
        auto at = clock_();
        capture_.record("stream", "authorization response", *auth, at);
        ingest(*auth, at);
        const auto decoded = AlpacaPaperDecoder{}.stream(*auth, BrokerTimestamp{at.utc});
        require(decoded.kind == PaperStreamKind::authorization && decoded.authorized,
                "paper authorization denied or unexpected response");
        wire_.subscribe_trade_updates();
        std::vector<std::pair<std::string, PaperReceipt>> pending;
        bool subscribed = false;
        for (unsigned i = 0; i < 32; ++i) {
            auto raw = wire_.poll(std::chrono::seconds{1});
            require(raw.has_value(), "paper subscription response deadline");
            at = clock_();
            capture_.record("stream", "subscription/queued update", *raw, at);
            const auto message = [&] {
                try {
                    return AlpacaPaperDecoder{}.stream(*raw, BrokerTimestamp{at.utc});
                } catch (...) {
                    static_cast<void>(runtime_.receive(*raw, at.utc));
                    throw;
                }
            }();
            if (message.kind == PaperStreamKind::trade_update) {
                pending.emplace_back(std::move(*raw), at);
                continue;
            }
            require(message.kind == PaperStreamKind::listening && message.trade_updates_subscribed,
                    "trade_updates subscription not acknowledged");
            ingest(*raw, at);
            subscribed = true;
            break;
        }
        require(subscribed, "subscription interleaving bound exhausted");
        for (const auto &[raw, at_pending] : pending)
            ingest(raw, at_pending);
        active_ = true;
    } catch (...) {
        fault();
        throw;
    }
}

PaperHttpClient::PaperHttpClient(PaperWire &wire, PaperCapture &capture, Clock clock, Wait wait)
    : wire_{wire}, capture_{capture}, clock_{std::move(clock)}, wait_{std::move(wait)} {
    require(clock_ && wait_, "HTTP clock and wait policy are required");
}

PaperHttpResponse PaperSession::exchange(const PaperRequest &r) { return http_.exchange(r); }

PaperHttpResponse PaperHttpClient::exchange(const PaperRequest &r, const std::function<void()> &eligible) {
    static_cast<void>(validate_paper_request(r));
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        capture_.record("http_request", r.method + " " + r.url, r.body, clock_());
        if (eligible)
            eligible();
        auto response = wire_.request(r);
        capture_.record("http_response", r.method + " " + r.url, response.body, clock_(), response.status,
                        response.request_id, response.retry_after);
        if (response.status != 429 || r.method != "GET" || attempt == 2)
            return response;
        unsigned seconds = 1;
        if (!response.retry_after.empty()) {
            const auto parsed =
                std::from_chars(response.retry_after.data(),
                                response.retry_after.data() + response.retry_after.size(), seconds);
            require(parsed.ec == std::errc{} &&
                        parsed.ptr == response.retry_after.data() + response.retry_after.size(),
                    "unsupported Retry-After representation");
        }
        require(seconds <= 5, "Retry-After exceeds bounded GET wait");
        wait_(std::chrono::seconds{seconds});
    }
    throw std::logic_error("unreachable HTTP retry state");
}

PaperHttpResponse PaperSession::read(const PaperRequest &r) {
    require(r.method == "GET", "read path permits only GET");
    try {
        auto result = exchange(r);
        require(result.status == 200, "paper GET did not return 200");
        return result;
    } catch (...) {
        fault();
        throw;
    }
}

std::size_t PaperSession::drain(std::chrono::milliseconds budget) {
    require(active_, "paper stream is not active");
    std::size_t count = 0;
    try {
        for (; count < 100; ++count) {
            auto message = wire_.poll(budget);
            if (!message)
                return count;
            const auto at = clock_();
            capture_.record("stream", "trade/account update", *message, at);
            ingest(*message, at);
        }
        throw std::runtime_error("paper stream drain bound exhausted");
    } catch (...) {
        fault();
        throw;
    }
}

bool PaperSession::synchronize() {
    try {
        require(active_, "paper synchronization requires an active stream");
        const AlpacaPaperRequests factory{};
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            static_cast<void>(drain());
            const auto generation = trades_;
            const auto a = read(factory.account()), p = read(factory.positions()),
                       o = read(factory.open_orders());
            static_cast<void>(drain());
            const auto b = read(factory.account()), q = read(factory.positions()),
                       v = read(factory.open_orders());
            static_cast<void>(drain());
            if (generation != trades_ || projection(a, p, o) != projection(b, q, v))
                continue;
            return runtime_.reconcile(b.body, q.body, v.body).outcome == execution::Outcome::applied;
        }
        fault();
        return false;
    } catch (...) {
        fault();
        throw;
    }
}

bool PaperSession::check_stream(std::chrono::milliseconds budget) {
    std::optional<std::int64_t> start;
    unsigned empty_polls = 0, messages = 0;
    std::string phase = "preflight";
    try {
        require(active_ && budget.count() > 0 && budget <= std::chrono::seconds{10},
                "stream check requires an active session and a bounded budget");
        static_cast<void>(drain());
        while (auto stale = wire_.take_pong())
            capture_.record("websocket_pong", "unmatched", *stale, clock_());
        phase = "ping_send";
        const auto sent = clock_();
        require(health_sequence_ < std::numeric_limits<std::uint64_t>::max(), "heartbeat sequence exhausted");
        const auto token =
            "paper-health-" + std::to_string(++health_sequence_) + "-" + std::to_string(sent.monotonic_ms);
        start = sent.monotonic_ms;
        capture_.record("websocket_ping", "heartbeat challenge", token, sent);
        wire_.ping(token, budget);
        phase = "awaiting_pong";
        capture_.record("connection", "websocket_ping_send_completed", "", clock_());
        // An empty poll is not a message and need not consume the full response budget.
        // Bound work independently so a faulty zero-progress wire cannot spin forever.
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const auto elapsed = clock_().monotonic_ms - *start;
            require(elapsed >= 0 && elapsed < budget.count(), "stream check response deadline");
            auto raw =
                wire_.poll(std::chrono::milliseconds{std::min<std::int64_t>(250, budget.count() - elapsed)});
            const auto at = clock_();
            if (raw) {
                ++messages;
                capture_.record("stream", "trade/account update during heartbeat", *raw, at);
                ingest(*raw, at);
            } else
                ++empty_polls;
            if (auto pong = wire_.take_pong()) {
                const bool matched = *pong == token;
                capture_.record("websocket_pong", matched ? "matched" : "unmatched", *pong, at);
                if (!matched)
                    continue;
                require(at.monotonic_ms - *start >= 0 && at.monotonic_ms - *start < budget.count(),
                        "WebSocket PONG arrived beyond deadline");
                phase = "reconciliation";
                const bool reconciled = synchronize();
                capture_.record("connection",
                                reconciled ? "stream_check_reconciled" : "stream_check_mismatch", "",
                                clock_());
                return reconciled;
            }
        }
        throw std::runtime_error("stream check poll/interleaving bound exhausted");
    } catch (...) {
        fault();
        try {
            const auto at = clock_();
            const auto elapsed = start ? std::to_string(at.monotonic_ms - *start) : "unavailable";
            capture_.record("connection",
                            "stream_check_failed phase=" + phase + " elapsed_ms=" + elapsed +
                                " empty_polls=" + std::to_string(empty_polls) +
                                " messages=" + std::to_string(messages),
                            "", at);
        } catch (...) { /* Preserve the primary failure if capture itself is unavailable. */
        }
        throw;
    }
}

PaperHttpResponse PaperSession::lookup(std::string client) {
    try {
        auto response = exchange(AlpacaPaperRequests{}.lookup(client));
        if (response.status == 200) {
            const auto result = runtime_.observe_order(response.body);
            require(result.outcome == execution::Outcome::applied, "lookup differs from durable intent");
        } else {
            fault();
        }
        return response;
    } catch (...) {
        fault();
        throw;
    }
}

PaperHttpResponse PaperSession::submit(const execution::OrderRequest &order,
                                       const std::function<void()> &eligible) {
    try {
        require(active_, "submit requires active paper stream");
        static_cast<void>(drain());
        if (eligible)
            eligible();
        require(runtime_.submit(order).outcome == execution::Outcome::applied, "durable order intent denied");
        auto response = http_.exchange(AlpacaPaperRequests{}.submit(order), eligible);
        require(response.status == 200, "submission outcome requires lookup/reconciliation; intent retained");
        require(runtime_.observe_order(response.body).outcome == execution::Outcome::applied,
                "submission response contradicts durable intent");
        return response;
    } catch (...) {
        fault();
        throw;
    }
}

PaperHttpResponse PaperSession::cancel(std::string client, std::string cancel_id) {
    try {
        require(active_, "cancel requires active paper stream");
        const auto state = runtime_.snapshot();
        const auto broker = state.at("broker_bindings").at(client).get<std::string>();
        require(runtime_.request_cancel(client, std::move(cancel_id)).outcome == execution::Outcome::applied,
                "durable cancellation denied");
        auto response = exchange(AlpacaPaperRequests{}.cancel(broker));
        require(response.status == 204, "cancel outcome unresolved; reservation retained");
        return response;
    } catch (...) {
        fault();
        throw;
    }
}
} // namespace trading
