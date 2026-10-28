#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace trading::execution {
inline constexpr std::size_t maximum_peer_payload_bytes = 65536 - 4;
// Owned native child and inherited anonymous pipes; no shell or sockets.
class ChildChannel {
  public:
    ChildChannel(const std::filesystem::path &executable, const std::vector<std::string> &arguments);
    ~ChildChannel();
    ChildChannel(ChildChannel &&) noexcept;
    ChildChannel &operator=(ChildChannel &&) noexcept;
    ChildChannel(const ChildChannel &) = delete;
    [[nodiscard]] std::string exchange(std::string payload, std::chrono::milliseconds budget);
    [[nodiscard]] std::uint64_t process_id() const noexcept;
    void stop() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trading::execution
