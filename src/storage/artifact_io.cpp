#include "artifact_io.hpp"

#include <fstream>
#include <stdexcept>

namespace trading {

std::string read_text_file(const std::filesystem::path& path, std::size_t maximum_bytes) {
    const auto size = std::filesystem::file_size(path);
    if (size > maximum_bytes) {
        throw std::invalid_argument("artifact exceeds its read bound: " + path.string());
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) { throw std::runtime_error("cannot open artifact: " + path.string()); }
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error("artifact read failed or size changed: " + path.string());
    }
    return text;
}

void write_new_text_file(const std::filesystem::path& path, std::string_view text) {
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("refusing to replace an existing artifact: " + path.string());
    }
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(temporary, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    std::filesystem::rename(temporary, path);
}

std::filesystem::path create_unique_directory(const std::filesystem::path& root, std::string_view stem) {
    const std::filesystem::path name{std::string{stem}};
    if (root.empty() || stem.empty() || name.filename() != name || stem == "." || stem == "..") {
        throw std::invalid_argument("run directory requires a root and a simple filename stem");
    }
    std::filesystem::create_directories(root);
    for (std::size_t suffix = 0; suffix < 1000; ++suffix) {
        const auto candidate = root / (std::string{stem} + "_" + std::to_string(suffix));
        if (std::filesystem::create_directory(candidate)) { return candidate; }
    }
    throw std::runtime_error("no unused run directory is available");
}

std::filesystem::path contained_path(const std::filesystem::path& root, std::string_view relative) {
    const std::filesystem::path path{std::string{relative}};
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
        relative.find('\\') != std::string_view::npos || relative.find(':') != std::string_view::npos) {
        throw std::invalid_argument("artifact reference must be relative to its dataset root");
    }
    auto current = root;
    for (const auto& part : path) {
        if (part == "." || part == ".." || part.empty()) {
            throw std::invalid_argument("artifact reference contains an invalid path component");
        }
        current /= part;
        if (std::filesystem::is_symlink(current)) {
            throw std::invalid_argument("dataset artifact references must not traverse symbolic links");
        }
    }
    return current;
}

} // namespace trading
