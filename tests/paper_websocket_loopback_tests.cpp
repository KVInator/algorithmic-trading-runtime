#include "paper_websocket.hpp"
#include <iostream>

namespace trading {
class PaperWebSocketTestAccess {
public:
    static void open(PaperWebSocket& socket, const std::string& port) {
        if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != port.npos)
            throw std::invalid_argument("invalid loopback port");
        socket.connect("ws://127.0.0.1:" + port + "/", true);
    }
};
} // namespace trading

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("loopback client requires port and case");
        const std::string mode{argv[2]};
        trading::PaperWebSocket socket;
        try {
            trading::PaperWebSocketTestAccess::open(socket, argv[1]);
        } catch (const std::exception&) {
            if (mode == "redirect")
                return 0;
            throw;
        }
        if (mode == "redirect")
            throw std::logic_error("redirect unexpectedly accepted");
        socket.send_json("{\"hello\":true}");
        socket.ping("loopback-challenge", std::chrono::seconds{1});
        if (mode == "quiet") {
            const auto start = std::chrono::steady_clock::now();
            if (socket.poll(std::chrono::milliseconds{100}))
                throw std::logic_error("quiet socket fabricated evidence");
            if (std::chrono::steady_clock::now() - start < std::chrono::milliseconds{100})
                throw std::logic_error("quiet poll returned prematurely");
            return 0;
        }
        bool data = false, wrong = false, matched = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < end) {
            try {
                const auto value = socket.poll(std::chrono::milliseconds{100});
                if (!value)
                    continue;
                if (value->pong) {
                    if (value->payload == "loopback-challenge")
                        matched = true;
                    else
                        wrong = true;
                } else {
                    if (value->payload != "{\"message\":\"ok\"}")
                        throw std::logic_error("fragmented data changed");
                    data = true;
                }
            } catch (const std::runtime_error&) {
                if (mode == "close")
                    return 0;
                throw;
            } catch (const std::invalid_argument& e) {
                if (mode == "oversize" && std::string{e.what()}.find("oversized") != std::string::npos)
                    return 0;
                throw;
            }
            if (mode == "roundtrip" && data && wrong && matched)
                return 0;
        }
        throw std::logic_error("expected wire evidence absent");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
