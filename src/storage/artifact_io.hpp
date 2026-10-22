#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace trading {

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path,
                                         std::size_t maximum_bytes = 4U * 1024U * 1024U);
// New artifacts only. A failed write leaves an unpublished .tmp file.
void write_new_text_file(const std::filesystem::path& path, std::string_view text);
[[nodiscard]] std::filesystem::path create_unique_directory(
    const std::filesystem::path& root, std::string_view stem);
[[nodiscard]] std::filesystem::path contained_path(
    const std::filesystem::path& root, std::string_view relative);

} // namespace trading
