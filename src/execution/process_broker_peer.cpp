#include "process_broker_peer.hpp"
#include <stdexcept>

namespace trading::execution {
namespace {
std::vector<std::string> arguments(const std::filesystem::path &journal, std::string account,
                                   std::vector<std::string> extra) {
    std::vector<std::string> args{journal.string(), std::move(account)};
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}
} // namespace
ProcessBrokerPeer::ProcessBrokerPeer(const std::filesystem::path &exe, const std::filesystem::path &journal,
                                     std::string account, std::vector<std::string> extra,
                                     std::chrono::milliseconds budget)
    : channel_(exe, arguments(journal, std::move(account), std::move(extra))), budget_(budget) {}
nlohmann::json decode_broker_reply(std::string_view payload, std::uint64_t expected_id) {
    const auto response = nlohmann::json::parse(payload);
    if (!response.is_object() || response.size() != 3 || !response.at("protocol").is_number_unsigned() ||
        response.at("protocol").get<std::uint64_t>() != 1 || !response.at("id").is_number_unsigned() ||
        response.at("id").get<std::uint64_t>() != expected_id) {
        throw std::runtime_error("peer response version or correlation differs");
    }
    return response.at("response");
}
nlohmann::json ProcessBrokerPeer::exchange(const nlohmann::json &command) {
    try {
        const auto sequence = ++sequence_;
        return decode_broker_reply(
            channel_.exchange(nlohmann::json{{"protocol", 1}, {"id", sequence}, {"command", command}}.dump(),
                              budget_),
            sequence);
    } catch (...) {
        channel_.stop();
        throw;
    }
}
std::uint64_t ProcessBrokerPeer::process_id() const noexcept {
    return channel_.process_id();
}
void ProcessBrokerPeer::stop() noexcept {
    channel_.stop();
}
} // namespace trading::execution
