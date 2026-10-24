#include "paper_websocket.hpp"
#include <curl/curl.h>
#include <curl/websockets.h>
#include <array>
#include <thread>

namespace trading {
namespace {
void checked(CURLcode value, const char* operation) {
    if (value != CURLE_OK)
        throw std::runtime_error(std::string{operation} + ": " + curl_easy_strerror(value));
}

class CurlRuntime {
public:
    CurlRuntime() { checked(curl_global_init(CURL_GLOBAL_DEFAULT), "WebSocket library initialization"); }

    ~CurlRuntime() { curl_global_cleanup(); }
};

struct Closer {
    void operator()(CURL* p) const noexcept {
        if (p)
            curl_easy_cleanup(p);
    }
};

using Clock = std::chrono::steady_clock;
} // namespace

class PaperWebSocket::Implementation {
public:
    std::unique_ptr<CURL, Closer> handle;
    PaperMessageAssembler assembler;
    std::string control;
    int control_kind = 0;

    Implementation() {
        static CurlRuntime library;
        static_cast<void>(library);
    }

    void send(std::string_view data, unsigned flags, std::chrono::milliseconds budget) {
        if (!handle || data.size() > 4096 || budget.count() <= 0)
            throw std::invalid_argument("invalid WebSocket send");
        const auto end = Clock::now() + budget;
        std::size_t offset = 0;
        do {
            if (Clock::now() >= end)
                throw std::runtime_error("WebSocket send deadline");
            std::size_t sent = 0;
            const auto code =
                curl_ws_send(handle.get(), data.data() + offset, data.size() - offset, &sent, 0, flags);
            if (code != CURLE_OK && code != CURLE_AGAIN)
                checked(code, "WebSocket send");
            if (sent > data.size() - offset)
                throw std::runtime_error("invalid WebSocket send length");
            offset += sent;
            if (code == CURLE_AGAIN || !sent)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        } while (offset < data.size());
    }
};

PaperWebSocket::PaperWebSocket() : implementation_{std::make_unique<Implementation>()} {}

PaperWebSocket::~PaperWebSocket() = default;

void PaperWebSocket::close() noexcept {
    implementation_->handle.reset();
    implementation_->assembler.reset();
    implementation_->control.clear();
    implementation_->control_kind = 0;
}

void PaperWebSocket::open() { connect("wss://paper-api.alpaca.markets/stream", false); }

void PaperWebSocket::connect(std::string_view url, bool loopback_test) {
    close();
    if ((!loopback_test && url != "wss://paper-api.alpaca.markets/stream") ||
        (loopback_test && !url.starts_with("ws://127.0.0.1:")))
        throw std::invalid_argument("unsupported WebSocket origin");
    auto& impl = *implementation_;
    impl.handle.reset(curl_easy_init());
    if (!impl.handle)
        throw std::runtime_error("WebSocket allocation failed");
    const std::string address{url};
    try {
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_URL, address.c_str()), "WebSocket URL");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_PROTOCOLS_STR, loopback_test ? "ws" : "wss"),
                "WebSocket protocols");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_FOLLOWLOCATION, 0L), "WebSocket redirects");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_CONNECT_ONLY, 2L), "WebSocket upgrade");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 10000L),
                "WebSocket connect timeout");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_TIMEOUT_MS, 20000L),
                "WebSocket handshake timeout");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_SSL_VERIFYPEER, 1L), "WebSocket certificate");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_SSL_VERIFYHOST, 2L), "WebSocket hostname");
        checked(curl_easy_setopt(impl.handle.get(), CURLOPT_WS_OPTIONS, static_cast<long>(CURLWS_NOAUTOPONG)),
                "WebSocket controls");
        if (loopback_test)
            checked(curl_easy_setopt(impl.handle.get(), CURLOPT_PROXY, ""), "loopback proxy isolation");
        checked(curl_easy_perform(impl.handle.get()), "WebSocket handshake");
        long code = 0;
        checked(curl_easy_getinfo(impl.handle.get(), CURLINFO_RESPONSE_CODE, &code), "WebSocket status");
        if (code != 101)
            throw std::runtime_error("WebSocket upgrade did not return 101");
    } catch (...) {
        close();
        throw;
    }
}

void PaperWebSocket::send_json(std::string_view payload) {
    try {
        implementation_->send(payload, CURLWS_TEXT, std::chrono::seconds{10});
    } catch (...) {
        close();
        throw;
    }
}

void PaperWebSocket::ping(std::string_view token, std::chrono::milliseconds budget) {
    if (token.empty() || token.size() > 125)
        throw std::invalid_argument("invalid WebSocket challenge length");
    try {
        implementation_->send(token, CURLWS_PING, budget);
    } catch (...) {
        close();
        throw;
    }
}

std::optional<PaperSocketMessage> PaperWebSocket::poll(std::chrono::milliseconds budget) {
    auto& impl = *implementation_;
    if (!impl.handle || budget.count() < 0)
        throw std::invalid_argument("WebSocket not connected or invalid poll");
    const auto end = Clock::now() + budget;
    try {
        for (unsigned reads = 0; reads < 4096; ++reads) {
            std::array<char, 4096> buffer{};
            std::size_t size = 0;
            const curl_ws_frame* meta = nullptr;
            const auto code = curl_ws_recv(impl.handle.get(), buffer.data(), buffer.size(), &size, &meta);
            if (code == CURLE_AGAIN) {
                if (Clock::now() >= end)
                    return std::nullopt;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }
            checked(code, "WebSocket receive");
            if (!meta || size > buffer.size())
                throw std::runtime_error("invalid WebSocket metadata");
            const auto flags = meta->flags;
            const auto left = meta->bytesleft;
            const auto bytes = std::string_view{buffer.data(), size};
            if (flags & CURLWS_CLOSE)
                throw std::runtime_error("WebSocket peer closed");
            if (flags & (CURLWS_PING | CURLWS_PONG)) {
                const auto kind = flags & (CURLWS_PING | CURLWS_PONG);
                if ((impl.control_kind && impl.control_kind != kind) || impl.control.size() + size > 125 ||
                    (flags & CURLWS_CONT))
                    throw std::runtime_error("invalid WebSocket control frame");
                impl.control_kind = kind;
                impl.control.append(bytes);
                if (left == 0) {
                    auto value = std::move(impl.control);
                    impl.control.clear();
                    impl.control_kind = 0;
                    if (kind & CURLWS_PONG)
                        return PaperSocketMessage{std::move(value), true};
                    impl.send(value, CURLWS_PONG, std::chrono::seconds{1});
                }
            } else {
                const bool binary = (flags & CURLWS_BINARY) != 0;
                if (!binary && !(flags & CURLWS_TEXT))
                    throw std::runtime_error("unsupported WebSocket data type");
                if (auto value = impl.assembler.push(bytes, binary, left == 0 && !(flags & CURLWS_CONT)))
                    return PaperSocketMessage{std::move(*value), false};
            }
            if (Clock::now() >= end)
                return std::nullopt;
        }
        throw std::runtime_error("WebSocket receive work bound exhausted");
    } catch (...) {
        close();
        throw;
    }
}
} // namespace trading
