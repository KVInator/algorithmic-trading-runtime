#include "alpaca_console_credentials.hpp"
#include "alpaca_credential_store_windows.hpp"

#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

class ConsoleEchoGuard {
public:
    ConsoleEchoGuard() : input_{GetStdHandle(STD_INPUT_HANDLE)} {
        if (input_ == nullptr || input_ == INVALID_HANDLE_VALUE ||
            !GetConsoleMode(input_, &original_mode_)) {
            throw std::runtime_error("credential input requires an interactive Windows console");
        }
        if (!SetConsoleMode(input_, original_mode_ & ~static_cast<DWORD>(ENABLE_ECHO_INPUT))) {
            const auto code = GetLastError();
            throw std::system_error(static_cast<int>(code), std::system_category(), "SetConsoleMode");
        }
    }

    ConsoleEchoGuard(const ConsoleEchoGuard&) = delete;
    ConsoleEchoGuard& operator=(const ConsoleEchoGuard&) = delete;

    ~ConsoleEchoGuard() {
        if (!SetConsoleMode(input_, original_mode_)) {
            std::cerr << "Warning: console input echo could not be restored\n";
        }
    }

private:
    HANDLE input_;
    DWORD original_mode_{};
};

std::string read_hidden_line(std::string_view prompt) {
    std::cout << prompt << std::flush;
    std::string value;
    {
        const ConsoleEchoGuard guard;
        if (!std::getline(std::cin, value)) {
            throw std::runtime_error("credential input was interrupted");
        }
    }
    std::cout << '\n';
    return value;
}

} // namespace

trading::AlpacaCredentials load_alpaca_console_credentials() {
    auto saved = trading::load_saved_alpaca_paper_credentials();
    if (saved) {
        std::cout << "Credentials: Windows Credential Manager\n";
        return std::move(*saved);
    }

    std::cout << "No saved Alpaca paper credential found. Using hidden console input.\n";
    auto key = read_hidden_line("Alpaca paper API key: ");
    auto secret = read_hidden_line("Alpaca paper API secret: ");
    return trading::AlpacaCredentials{std::move(key), std::move(secret)};
}
