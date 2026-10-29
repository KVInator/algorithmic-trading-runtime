#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace trading {

class Sha256 {
  public:
    Sha256();
    ~Sha256();
    void update(std::string_view bytes);
    [[nodiscard]] std::string finish();

  private:
    class State;
    std::unique_ptr<State> state_;
};

[[nodiscard]] std::string sha256(std::string_view bytes);
// Preserve the ordinary artifact bound; callers opt into larger registered files.
[[nodiscard]] std::string sha256_file(const std::filesystem::path &path,
                                      std::uintmax_t maximum_bytes = 4U * 1024U * 1024U);

} // namespace trading
