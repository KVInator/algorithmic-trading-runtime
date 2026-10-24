#include "alpaca_paper_wire.hpp"
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <algorithm>
#include <condition_variable>
#include <mutex>
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

std::wstring wide(std::string_view value) {
    return {value.begin(), value.end()};
}

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

// The self-reference is released only by HANDLE_CLOSING, WinHTTP's final callback.
// This retains buffers/context if the caller times out with asynchronous I/O pending.
struct CallbackState : std::enable_shared_from_this<CallbackState> {
    std::mutex mutex;
    std::condition_variable changed;
    std::array<bool, 4> ready{};
    DWORD error = 0;
    bool closed = false;
    std::array<char, 4096> buffer{};
    DWORD bytes = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
    std::string outbound;
    std::shared_ptr<CallbackState> until_closed;

    ~CallbackState() {
        if (!outbound.empty())
            SecureZeroMemory(outbound.data(), outbound.size());
    }

    static std::optional<std::size_t> index(DWORD event) {
        switch (event) {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            return 0;
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            return 1;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            return 2;
        case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
            return 3;
        default:
            return std::nullopt;
        }
    }

    void prepare(std::size_t event) {
        std::lock_guard guard{mutex};
        ready[event] = false;
    }

    bool wait(std::size_t event, std::chrono::milliseconds budget) {
        std::unique_lock lock{mutex};
        if (!changed.wait_for(lock, budget, [&] { return ready[event] || error || closed; }))
            return false;
        if (error)
            fail("asynchronous paper socket", error);
        if (closed)
            throw std::runtime_error("paper socket closed");
        return true;
    }
};

void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD event, LPVOID info, DWORD length) noexcept {
    if (!context)
        return;
    auto* pointer = reinterpret_cast<CallbackState*>(context);
    auto keep = pointer->shared_from_this();
    if (!keep)
        return;
    {
        std::lock_guard guard{keep->mutex};
        if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            keep->closed = true;
            keep->until_closed.reset();
        } else if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && length >= sizeof(WINHTTP_ASYNC_RESULT)) {
            keep->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        } else if (const auto index = CallbackState::index(event)) {
            if (event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
                length >= sizeof(WINHTTP_WEB_SOCKET_STATUS)) {
                const auto* value = static_cast<WINHTTP_WEB_SOCKET_STATUS*>(info);
                keep->bytes = value->dwBytesTransferred;
                keep->type = value->eBufferType;
            }
            keep->ready[*index] = true;
        }
    }
    keep->changed.notify_all();
}

void register_callback(HINTERNET p, const std::shared_ptr<CallbackState>& state) {
    const DWORD_PTR context = reinterpret_cast<DWORD_PTR>(state.get());
    if (!WinHttpSetOption(p, WINHTTP_OPTION_CONTEXT_VALUE, const_cast<DWORD_PTR*>(&context), sizeof(context)))
        fail("paper callback context");
    if (WinHttpSetStatusCallback(p, callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) ==
        WINHTTP_INVALID_STATUS_CALLBACK)
        fail("paper status callback");
    state->until_closed = state;
}

void initiated(BOOL success) {
    if (!success && GetLastError() != ERROR_IO_PENDING)
        fail("asynchronous paper handshake");
}

void initiated(DWORD result) {
    if (result != NO_ERROR && result != ERROR_IO_PENDING)
        fail("asynchronous paper WebSocket", result);
}
} // namespace

class AlpacaPaperWire::Implementation {
public:
    explicit Implementation(AlpacaCredentials c) : credentials{std::move(c)} {}

    AlpacaCredentials credentials;
    Handle session, connection, socket;
    std::shared_ptr<CallbackState> state;
    bool pending = false;
    PaperMessageAssembler assembler;

    void guard_secrets(std::string_view body) const {
        if (body.find(credentials.key()) != body.npos || body.find(credentials.secret()) != body.npos)
            throw std::runtime_error("credential echo refused before archival");
    }

    void close() noexcept {
        socket.reset();
        connection.reset();
        session.reset();
        state.reset();
        pending = false;
        assembler.reset();
    }

    void send(std::string payload) {
        if (!socket)
            throw std::runtime_error("paper socket is not connected");
        state->outbound = std::move(payload);
        state->prepare(3);
        try {
            initiated(WinHttpWebSocketSend(socket.get(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                           state->outbound.data(),
                                           static_cast<DWORD>(state->outbound.size())));
            if (!state->wait(3, std::chrono::seconds{10}))
                throw std::runtime_error("paper WebSocket send deadline");
            SecureZeroMemory(state->outbound.data(), state->outbound.size());
            state->outbound.clear();
        } catch (...) {
            close();
            throw;
        }
    }
};

AlpacaPaperWire::AlpacaPaperWire(AlpacaCredentials credentials)
    : implementation_{std::make_unique<Implementation>(std::move(credentials))} {}

AlpacaPaperWire::~AlpacaPaperWire() {
    close();
}

void AlpacaPaperWire::close() noexcept {
    implementation_->close();
}

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
        impl.session =
            handle(WinHttpOpen(L"AlgorithmicTradingRuntime/PaperStream", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC),
                   "paper stream session");
        if (!WinHttpSetTimeouts(impl.session.get(), 10000, 10000, 10000, 20000))
            fail("paper handshake timeouts");
        impl.connection = handle(
            WinHttpConnect(impl.session.get(), L"paper-api.alpaca.markets", INTERNET_DEFAULT_HTTPS_PORT, 0),
            "paper stream connection");
        auto request =
            handle(WinHttpOpenRequest(impl.connection.get(), L"GET", L"/stream", nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
                   "paper upgrade request");
        redirects(request.get());
        if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
            fail("paper WebSocket option");
        auto handshake = std::make_shared<CallbackState>();
        register_callback(request.get(), handshake);
        handshake->prepare(0);
        initiated(WinHttpSendRequest(request.get(), L"Accept: application/json\r\n", static_cast<DWORD>(-1L),
                                     WINHTTP_NO_REQUEST_DATA, 0, 0,
                                     reinterpret_cast<DWORD_PTR>(handshake.get())));
        if (!handshake->wait(0, std::chrono::seconds{20}))
            throw std::runtime_error("paper handshake send deadline");
        handshake->prepare(1);
        initiated(WinHttpReceiveResponse(request.get(), nullptr));
        if (!handshake->wait(1, std::chrono::seconds{20}))
            throw std::runtime_error("paper handshake response deadline");
        if (status(request.get()) != 101)
            throw std::runtime_error("paper WebSocket upgrade did not return 101");
        impl.state = std::make_shared<CallbackState>();
        impl.state->until_closed = impl.state;
        auto raw =
            WinHttpWebSocketCompleteUpgrade(request.get(), reinterpret_cast<DWORD_PTR>(impl.state.get()));
        if (!raw) {
            impl.state->until_closed.reset();
            fail("paper WebSocket upgrade");
        }
        impl.socket = Handle{raw};
        request.reset();
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

std::optional<std::string> AlpacaPaperWire::poll(std::chrono::milliseconds budget) {
    auto& impl = *implementation_;
    if (!impl.socket)
        throw std::runtime_error("paper WebSocket is not connected");
    const auto deadline = std::chrono::steady_clock::now() + budget;
    try {
        for (;;) {
            if (!impl.pending) {
                impl.state->prepare(2);
                initiated(WinHttpWebSocketReceive(impl.socket.get(), impl.state->buffer.data(),
                                                  static_cast<DWORD>(impl.state->buffer.size()), nullptr,
                                                  nullptr));
                impl.pending = true;
            }
            const auto remaining =
                std::max(std::chrono::milliseconds{0}, std::chrono::ceil<std::chrono::milliseconds>(
                                                           deadline - std::chrono::steady_clock::now()));
            if (!impl.state->wait(2, remaining))
                return std::nullopt;
            impl.pending = false;
            const auto type = impl.state->type;
            const auto size = impl.state->bytes;
            if (size > impl.state->buffer.size())
                throw std::runtime_error("invalid WebSocket completion length");
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                throw std::runtime_error("paper WebSocket peer closed");
            const bool binary = type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                                type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
            const bool text = type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                              type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE;
            if (!binary && !text)
                throw std::runtime_error("unsupported WebSocket buffer type");
            const bool final = type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                               type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            if (auto message =
                    impl.assembler.push(std::string_view{impl.state->buffer.data(), size}, binary, final)) {
                impl.guard_secrets(*message);
                return message;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return std::nullopt;
        }
    } catch (...) {
        close();
        throw;
    }
}
} // namespace trading
