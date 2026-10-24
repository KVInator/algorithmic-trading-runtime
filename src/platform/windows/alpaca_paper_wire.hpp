#pragma once
#include "paper_transport.hpp"
#include "alpaca_credentials.hpp"

namespace trading {
// Serialized caller API. WinHTTP owns REST; PaperWebSocket owns streaming and PING/PONG.
class AlpacaPaperWire final : public PaperWire {
public:
    explicit AlpacaPaperWire(AlpacaCredentials credentials);
    ~AlpacaPaperWire() override;
    AlpacaPaperWire(const AlpacaPaperWire&) = delete;
    AlpacaPaperWire& operator=(const AlpacaPaperWire&) = delete;
    [[nodiscard]] PaperHttpResponse request(const PaperRequest&) override;
    void open_authenticated_stream() override;
    void subscribe_trade_updates() override;
    void ping(std::string_view token, std::chrono::milliseconds budget) override;
    [[nodiscard]] std::optional<std::string> take_pong() override;
    [[nodiscard]] std::optional<std::string> poll(std::chrono::milliseconds budget) override;
    void close() noexcept override;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};
} // namespace trading
