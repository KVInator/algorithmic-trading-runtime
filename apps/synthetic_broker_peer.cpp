#include "broker_service.hpp"
#include "child_channel.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    using namespace trading::execution;
    try {
#ifdef _WIN32
        if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) {
            throw std::runtime_error("cannot set binary peer pipes");
        }
#endif
        if (argc != 3 && argc != 5) {
            throw std::invalid_argument("usage: synthetic_broker_peer <journal> <account> "
                                        "[--stop-after-commit|--pause-after-commit <command-id>]");
        }
        const std::filesystem::path path{argv[1]};
        const std::string account{argv[2]};
        std::string mode, command;
        if (argc == 5) {
            mode = argv[3];
            command = argv[4];
            if (mode != "--stop-after-commit" && mode != "--pause-after-commit") {
                throw std::invalid_argument("unsupported diagnostic process fault");
            }
        }
        auto service = std::filesystem::exists(path)
                           ? BrokerService::recover(open_journal_file(path), account)
                           : BrokerService::create(create_journal_file(path), account);
        std::uint64_t previous = 0;
        while (true) {
            std::array<char, 4> prefix{};
            std::cin.read(prefix.data(), 4);
            if (std::cin.gcount() == 0 && std::cin.eof()) {
                return 0;
            }
            if (std::cin.gcount() != 4) {
                throw std::runtime_error("truncated peer request header");
            }
            std::uint32_t length = 0;
            for (const auto c : prefix) {
                length = (length << 8) | static_cast<unsigned char>(c);
            }
            if (!length || length > maximum_peer_payload_bytes) {
                throw std::runtime_error("peer request length exceeds contract");
            }
            std::string bytes(length, '\0');
            std::cin.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (std::cin.gcount() != static_cast<std::streamsize>(bytes.size())) {
                throw std::runtime_error("truncated peer request body");
            }
            const auto envelope = nlohmann::json::parse(bytes);
            if (!envelope.is_object() || envelope.size() != 3 ||
                !envelope.at("protocol").is_number_unsigned() ||
                envelope.at("protocol").get<std::uint64_t>() != 1 ||
                !envelope.at("id").is_number_unsigned() ||
                envelope.at("id").get<std::uint64_t>() != previous + 1) {
                throw std::invalid_argument("peer envelope version or sequence differs");
            }
            ++previous;
            const auto &input = envelope.at("command");
            const auto result = service.exchange(input);
            if (!command.empty() && input.value("command_id", "") == command) {
                if (mode == "--stop-after-commit") {
                    std::_Exit(86);
                }
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
            const auto output =
                nlohmann::json{{"protocol", 1}, {"id", previous}, {"response", result}}.dump();
            if (output.empty() || output.size() > maximum_peer_payload_bytes) {
                throw std::runtime_error("peer response exceeds contract");
            }
            const auto size = static_cast<std::uint32_t>(output.size());
            for (unsigned i = 0; i < 4; ++i) {
                prefix[i] = static_cast<char>((size >> (24 - 8 * i)) & 255U);
            }
            std::cout.write(prefix.data(), 4);
            std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
            std::cout.flush();
            if (!std::cout) {
                throw std::runtime_error("peer response pipe failed");
            }
        }
    } catch (const std::exception &error) {
        std::cerr << "Synthetic broker peer stopped: " << error.what() << '\n';
        return 1;
    }
}
