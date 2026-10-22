#include "alpaca_credentials.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// These are deliberately synthetic strings, not account credentials.
void test_valid_credentials() {
    const trading::AlpacaCredentials credentials{"SYNTHETIC_KEY", "SYNTHETIC_SECRET"};
    if (credentials.key() != "SYNTHETIC_KEY" || credentials.secret() != "SYNTHETIC_SECRET") {
        throw std::runtime_error("credential values must be retained without trimming or modification");
    }
}

void test_rejected_header_values() {
    const std::array invalid_values{
        std::string{}, std::string{" "}, std::string{"TOKEN "}, std::string{"TOKEN\t"},
        std::string{"TOKEN\r\nInjected: value"}, std::string{"TOKEN\0MORE", 10},
        std::string(1, static_cast<char>(0x7f)), std::string(1, static_cast<char>(0xff))};

    for (const auto& invalid : invalid_values) {
        bool rejected_key = false;
        bool rejected_secret = false;
        try {
            static_cast<void>(trading::AlpacaCredentials{invalid, "SYNTHETIC_SECRET"});
        } catch (const std::invalid_argument& error) {
            rejected_key = true;
            if (std::string{error.what()}.find("Injected: value") != std::string::npos) {
                throw std::runtime_error("validation messages must not echo credential contents");
            }
        }
        try {
            static_cast<void>(trading::AlpacaCredentials{"SYNTHETIC_KEY", invalid});
        } catch (const std::invalid_argument&) {
            rejected_secret = true;
        }
        if (!rejected_key || !rejected_secret) {
            throw std::runtime_error("empty, whitespace, control and non-ASCII credentials must be rejected");
        }
    }
}

} // namespace

int main() {
    try {
        test_valid_credentials();
        test_rejected_header_values();
        std::cout << "Alpaca credential contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Alpaca credential contract failure: " << error.what() << '\n';
        return 1;
    }
}
