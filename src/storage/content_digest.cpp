#include "content_digest.hpp"
#include <array>
#include <fstream>
#include <stdexcept>

namespace trading {

std::string sha256(std::string_view bytes) {
    Sha256 digest;
    digest.update(bytes);
    return digest.finish();
}

std::string sha256_file(const std::filesystem::path &path, std::uintmax_t maximum_bytes) {
    const auto size = std::filesystem::file_size(path);
    if (size > maximum_bytes) {
        throw std::invalid_argument("artifact exceeds its hash bound: " + path.string());
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("cannot open artifact: " + path.string());
    }
    Sha256 digest;
    std::array<char, 64U * 1024U> buffer{};
    std::uintmax_t consumed = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > maximum_bytes - consumed) {
            throw std::invalid_argument("artifact exceeds its hash bound: " + path.string());
        }
        digest.update(std::string_view{buffer.data(), count});
        consumed += count;
    }
    if (!input.eof() || input.bad() || consumed != size) {
        throw std::runtime_error("artifact hash read failed or size changed: " + path.string());
    }
    return digest.finish();
}

} // namespace trading
