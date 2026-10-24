#pragma once
#include "alpaca_paper_contract.hpp"
#include "paper_execution.hpp"
#include <chrono>
#include <functional>
#include <filesystem>
#include <memory>

namespace trading {
struct PaperHttpResponse {
    unsigned status;
    std::string body;
    std::string retry_after;
    std::string request_id;
};

struct PaperReceipt {
    std::string utc;
    std::int64_t monotonic_ms;
};

// Runtime substitution is required for native sockets versus deterministic faults.
class PaperWire {
public:
    virtual ~PaperWire() = default;
    [[nodiscard]] virtual PaperHttpResponse request(const PaperRequest&) = 0;
    virtual void open_authenticated_stream() = 0; // Credentials stay inside the wire.
    virtual void subscribe_trade_updates() = 0;
    virtual void ping(std::string_view token, std::chrono::milliseconds budget) = 0;
    [[nodiscard]] virtual std::optional<std::string> take_pong() = 0;
    [[nodiscard]] virtual std::optional<std::string> poll(std::chrono::milliseconds budget) = 0;
    virtual void close() noexcept = 0;
};

// Defense at the credential-bearing transport, independent of request factories.
[[nodiscard]] std::string validate_paper_request(const PaperRequest& request);
[[nodiscard]] PaperReceipt paper_receipt_now();

class PaperMessageAssembler {
public:
    [[nodiscard]] std::optional<std::string> push(std::string_view bytes, bool binary, bool final);
    void reset() noexcept;

private:
    std::string pending_;
    std::optional<bool> binary_;
    std::size_t fragments_{0};
};

// New segment only. Each body is synchronized before its hashed index entry.
class PaperCapture {
public:
    explicit PaperCapture(std::filesystem::path directory);
    void record(std::string kind, std::string detail, std::string_view body, const PaperReceipt& receipt,
                unsigned status = 0, std::string request_id = {}, std::string retry_after = {});
    [[nodiscard]] std::size_t records() const noexcept;

private:
    std::filesystem::path directory_;
    std::unique_ptr<execution::JournalStorage> index_;
    std::size_t records_{0};
    std::size_t bytes_{0};
    std::size_t index_bytes_{0};
    std::string previous_ = std::string(64, '0');
};

class PaperHttpClient {
public:
    using Clock = std::function<PaperReceipt()>;
    using Wait = std::function<void(std::chrono::milliseconds)>;
    PaperHttpClient(PaperWire& wire, PaperCapture& capture, Clock clock, Wait wait);
    [[nodiscard]] PaperHttpResponse exchange(const PaperRequest& request,
                                             const std::function<void()>& eligible = {});

private:
    PaperWire& wire_;
    PaperCapture& capture_;
    Clock clock_;
    Wait wait_;
};

class PaperSession {
public:
    using Clock = std::function<PaperReceipt()>;
    using Wait = std::function<void(std::chrono::milliseconds)>;
    PaperSession(execution::JournaledPaperRuntime& runtime, PaperWire& wire, PaperCapture& capture,
                 Clock clock, Wait wait);
    void connect();
    [[nodiscard]] bool synchronize();
    [[nodiscard]] bool check_stream(std::chrono::milliseconds budget);
    [[nodiscard]] PaperHttpResponse read(const PaperRequest& request);
    [[nodiscard]] PaperHttpResponse submit(const execution::OrderRequest& cent_intent,
                                           const std::function<void()>& eligible = {});
    [[nodiscard]] PaperHttpResponse cancel(std::string client_id, std::string cancel_id);
    [[nodiscard]] PaperHttpResponse lookup(std::string client_id);
    [[nodiscard]] std::size_t drain(std::chrono::milliseconds quiet_budget = std::chrono::milliseconds{10});
    void disconnect();

private:
    [[nodiscard]] PaperHttpResponse exchange(const PaperRequest& request);
    void ingest(const std::string& message, const PaperReceipt& receipt);
    void fault() noexcept;
    execution::JournaledPaperRuntime& runtime_;
    PaperWire& wire_;
    PaperCapture& capture_;
    Clock clock_;
    PaperHttpClient http_;
    bool active_{false};
    std::size_t trades_{0};
    std::uint64_t health_sequence_{0};
};
} // namespace trading
