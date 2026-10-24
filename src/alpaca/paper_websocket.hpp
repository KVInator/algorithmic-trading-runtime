#pragma once
#include "paper_transport.hpp"

namespace trading {
struct PaperSocketMessage {
    std::string payload;
    bool pong;
};

// One serialized, bounded WebSocket owner. Public connection is paper-only.
class PaperWebSocket {
public:
    PaperWebSocket();
    ~PaperWebSocket();
    PaperWebSocket(const PaperWebSocket&) = delete;
    void open();
    void close() noexcept;
    void send_json(std::string_view payload);
    void ping(std::string_view token, std::chrono::milliseconds budget);
    [[nodiscard]] std::optional<PaperSocketMessage> poll(std::chrono::milliseconds budget);

private:
    friend class PaperWebSocketTestAccess;
    void connect(std::string_view url, bool loopback_test);
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};
} // namespace trading
