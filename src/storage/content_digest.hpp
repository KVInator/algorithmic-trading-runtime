#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace trading {

[[nodiscard]] std::string sha256(std::string_view bytes);
// Preserve the ordinary artifact bound; callers opt into larger registered files.
[[nodiscard]] std::string sha256_file(const std::filesystem::path& path,
    std::size_t maximum_bytes = 4U * 1024U * 1024U);

} // namespace trading
