#include "alpaca_paper_wire.hpp"
#include "paper_websocket.hpp"
#include <deque>
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <system_error>

namespace trading {
namespace {
struct Closer {
    void operator()(void* p) const noexcept {
        if (p)
            static_cast<void>(WinHttpCloseHandle(p));
    }
};

using Handle = std::unique_ptr<void, Closer>;

[[noreturn]] void fail(const char* operation, DWORD error = GetLastError()) {
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}

Handle handle(HINTERNET p, const char* operation) {
    if (!p)
        fail(operation);
    return Handle{p};
}

std::wstring wide(std::string_view value) { return {value.begin(), value.end()}; }

void redirects(HINTERNET p) {
    DWORD value = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(p, WINHTTP_OPTION_REDIRECT_POLICY, &value, sizeof(value)))
        fail("paper redirect policy");
}

unsigned status(HINTERNET p) {
    DWORD value = 0, length = sizeof(value);
    if (!WinHttpQueryHeaders(p, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &value, &length, WINHTTP_NO_HEADER_INDEX))
        fail("paper HTTP status");
    return static_cast<unsigned>(value);
}

std::string header(HINTERNET p, const wchar_t* name) {
    std::array<wchar_t, 257> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (!WinHttpQueryHeaders(p, WINHTTP_QUERY_CUSTOM, name, buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        const auto error = GetLastError();
        if (error == ERROR_WINHTTP_HEADER_NOT_FOUND)
            return {};
        fail("paper response header", error);
    }
    std::string out;
    for (const wchar_t c : buffer) {
        if (!c)
            break;
        if (c < 32 || c > 126)
            throw std::runtime_error("unsupported response header characters");
        out.push_back(static_cast<char>(c));
    }
    return out;
}

} // namespace

class AlpacaPaperWire::Implementation {
public:
    explicit Implementation(AlpacaCredentials c) : credentials{std::move(c)} {}

    AlpacaCredentials credentials;
    PaperWebSocket socket;
    std::deque<std::string> pongs;

    void guard_secrets(std::string_view body) const {
        if (body.find(credentials.key()) != body.npos || body.find(credentials.secret()) != body.npos)
            throw std::runtime_error("credential echo refused before archival");
    }

    void close() noexcept {
        socket.close();
        pongs.clear();
    }

    void send(std::string payload) {
        try {
            socket.send_json(payload);
        } catch (...) {
            SecureZeroMemory(payload.data(), payload.size());
            throw;
        }
        SecureZeroMemory(payload.data(), payload.size());
    }
};

AlpacaPaperWire::AlpacaPaperWire(AlpacaCredentials credentials)
    : implementation_{std::make_unique<Implementation>(std::move(credentials))} {}

AlpacaPaperWire::~AlpacaPaperWire() { close(); }

void AlpacaPaperWire::close() noexcept { implementation_->close(); }

PaperHttpResponse AlpacaPaperWire::request(const PaperRequest& request) {
    const auto path = wide(validate_paper_request(request));
    auto session = handle(WinHttpOpen(L"AlgorithmicTradingRuntime/Paper", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0),
                          "paper WinHttpOpen");
    if (!WinHttpSetTimeouts(session.get(), 10000, 10000, 10000, 20000))
        fail("paper HTTP timeouts");
    auto connection =
        handle(WinHttpConnect(session.get(), L"paper-api.alpaca.markets", INTERNET_DEFAULT_HTTPS_PORT, 0),
               "paper WinHttpConnect");
    const auto method = wide(request.method);
    auto http =
        handle(WinHttpOpenRequest(connection.get(), method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
               "paper HTTP request");
    redirects(http.get());
    const auto& credentials = implementation_->credentials;
    const auto headers = L"APCA-API-KEY-ID: " + wide(credentials.key()) + L"\r\nAPCA-API-SECRET-KEY: " +
                         wide(credentials.secret()) +
                         L"\r\nAccept: application/json\r\nContent-Type: application/json\r\n";
    if (headers.size() > 65536)
        throw std::runtime_error("paper credentials exceed header bound");
    if (!WinHttpSendRequest(
            http.get(), headers.c_str(), static_cast<DWORD>(headers.size()),
            request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
            static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0))
        fail("paper HTTP send");
    if (!WinHttpReceiveResponse(http.get(), nullptr))
        fail("paper HTTP response");
    PaperHttpResponse response{status(http.get()), "", header(http.get(), L"Retry-After"),
                               header(http.get(), L"X-Request-ID")};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                .count();
        if (remaining <= 0)
            throw std::runtime_error("paper HTTP body deadline");
        DWORD timeout = static_cast<DWORD>(remaining);
        if (!WinHttpSetOption(http.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout)))
            fail("paper body timeout");
        DWORD bytes = 0;
        if (!WinHttpReadData(http.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes))
            fail("paper HTTP body read");
        if (!bytes)
            break;
        if (response.body.size() + bytes > 1024U * 1024U)
            throw std::runtime_error("paper HTTP body exceeds one MiB");
        response.body.append(buffer.data(), bytes);
    }
    implementation_->guard_secrets(response.body);
    implementation_->guard_secrets(response.request_id);
    implementation_->guard_secrets(response.retry_after);
    return response;
}

void AlpacaPaperWire::open_authenticated_stream() {
    close();
    auto& impl = *implementation_;
    try {
        impl.socket.open();
        const auto& c = impl.credentials;
        impl.send(nlohmann::json{{"action", "auth"}, {"key", c.key()}, {"secret", c.secret()}}.dump());
    } catch (...) {
        close();
        throw;
    }
}

void AlpacaPaperWire::subscribe_trade_updates() {
    implementation_->send(R"({"action":"listen","data":{"streams":["trade_updates"]}})");
}

void AlpacaPaperWire::ping(std::string_view token, std::chrono::milliseconds budget) {
    implementation_->socket.ping(token, budget);
}

std::optional<std::string> AlpacaPaperWire::take_pong() {
    auto& queue = implementation_->pongs;
    if (queue.empty())
        return std::nullopt;
    auto value = std::move(queue.front());
    queue.pop_front();
    return value;
}

std::optional<std::string> AlpacaPaperWire::poll(std::chrono::milliseconds budget) {
    try {
        auto message = implementation_->socket.poll(budget);
        if (!message)
            return std::nullopt;
        implementation_->guard_secrets(message->payload);
        if (message->pong) {
            if (implementation_->pongs.size() >= 16)
                throw std::runtime_error("WebSocket PONG queue exhausted");
            implementation_->pongs.push_back(std::move(message->payload));
            return std::nullopt;
        }
        return message->payload;
    } catch (...) {
        close();
        throw;
    }
}
} // namespace trading
