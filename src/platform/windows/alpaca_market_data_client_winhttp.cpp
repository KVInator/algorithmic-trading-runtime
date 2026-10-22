#include "alpaca_market_data_client.hpp"
#include "alpaca_corporate_action_request.hpp"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace trading {
namespace {

struct InternetHandleCloser {
    void operator()(void* handle) const noexcept {
        static_cast<void>(WinHttpCloseHandle(handle));
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

[[noreturn]] void throw_windows_error(const char* operation) {
    const auto code = GetLastError();
    throw std::system_error(static_cast<int>(code), std::system_category(), operation);
}

InternetHandle require_handle(HINTERNET handle, const char* operation) {
    if (handle == nullptr) {
        throw_windows_error(operation);
    }
    return InternetHandle{handle};
}

std::wstring widen_ascii(std::string_view value) {
    return std::wstring{value.begin(), value.end()};
}

} // namespace

AlpacaMarketDataClient::AlpacaMarketDataClient(AlpacaCredentials credentials)
    : credentials_{std::move(credentials)} {}

MarketDataResponse AlpacaMarketDataClient::get_historical_bars(
    const HistoricalBarRequest& bar_request,
    const std::optional<std::string>& page_token) const {
    return get_page(false, bar_request.path(page_token));
}

MarketDataResponse AlpacaMarketDataClient::get_market_calendar(const CalendarRange& range) const {
    return get_page(true, range.path());
}

MarketDataResponse AlpacaMarketDataClient::get_corporate_action_probe(int year) const {
    return get_page(false, CorporateActionProbeRequest{year}.path());
}

MarketDataResponse AlpacaMarketDataClient::get_corporate_actions(const CorporateActionRequest& request,
    const std::optional<std::string>& token) const {
    return get_page(false, request.path(token));
}

MarketDataResponse AlpacaMarketDataClient::get_page(bool calendar, const std::string& request_path) const {
    const auto path = widen_ascii(request_path);
    auto session = require_handle(
        WinHttpOpen(L"AlgorithmicTradingRuntime/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0),
        "WinHttpOpen");

    if (!WinHttpSetTimeouts(session.get(), 10000, 10000, 10000, 20000)) {
        throw_windows_error("WinHttpSetTimeouts");
    }

    auto connection = require_handle(
        WinHttpConnect(session.get(), calendar ? L"paper-api.alpaca.markets" : L"data.alpaca.markets",
                       INTERNET_DEFAULT_HTTPS_PORT, 0),
        "WinHttpConnect");

    auto request = require_handle(
        WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
        "WinHttpOpenRequest");

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirect_policy, sizeof(redirect_policy))) {
        throw_windows_error("WinHttpSetOption redirect policy");
    }

    const std::wstring headers =
        L"APCA-API-KEY-ID: " + widen_ascii(credentials_.key()) +
        L"\r\nAPCA-API-SECRET-KEY: " + widen_ascii(credentials_.secret()) +
        L"\r\nAccept: application/json\r\n";

    if (headers.size() > std::numeric_limits<DWORD>::max()) {
        throw std::length_error("authentication headers exceed the WinHTTP size limit");
    }

    if (!WinHttpSendRequest(request.get(), headers.c_str(),
                            static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        throw_windows_error("WinHttpSendRequest");
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw_windows_error("WinHttpReceiveResponse");
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        throw_windows_error("WinHttpQueryHeaders");
    }

    std::array<char, 4096> buffer{};
    std::string body;
    while (true) {
        DWORD bytes_read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), &bytes_read)) {
            throw_windows_error("WinHttpReadData");
        }
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read > maximum_market_data_page_bytes - body.size()) {
            throw std::runtime_error("market-data response page exceeds the 64 KiB limit");
        }
        body.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }
    if (body.empty() && status == 200) {
        throw std::runtime_error("Alpaca returned an empty response body");
    }

    std::optional<std::string> retry_after;
    if (status == 429) {
        std::array<wchar_t, 256> header{};
        DWORD size = static_cast<DWORD>(sizeof(header));
        if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX,
                                header.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
            std::string value;
            for (const auto character : std::wstring_view{header.data()}) {
                if (static_cast<unsigned int>(character) > 127U) {
                    throw std::runtime_error("Retry-After header is not ASCII");
                }
                value.push_back(static_cast<char>(character));
            }
            retry_after = std::move(value);
        } else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) {
            throw_windows_error("WinHttpQueryHeaders Retry-After");
        }
    }
    return {static_cast<std::uint32_t>(status), std::move(body), std::move(retry_after)};
}

} // namespace trading
