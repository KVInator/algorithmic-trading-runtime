#pragma once
#include "broker_service.hpp"
#include "child_channel.hpp"
#include <string_view>

namespace trading::execution {
[[nodiscard]] nlohmann::json decode_broker_reply(std::string_view payload, std::uint64_t expected_id);
class ProcessBrokerPeer final : public BrokerPeer {
  public:
    ProcessBrokerPeer(const std::filesystem::path &executable, const std::filesystem::path &journal,
                      std::string account_id, std::vector<std::string> diagnostic_arguments = {},
                      std::chrono::milliseconds budget = std::chrono::seconds{5});
    [[nodiscard]] nlohmann::json exchange(const nlohmann::json &command) override;
    [[nodiscard]] std::uint64_t process_id() const noexcept;
    void stop() noexcept;

  private:
    ChildChannel channel_;
    std::chrono::milliseconds budget_;
    std::uint64_t sequence_{};
};
} // namespace trading::execution
