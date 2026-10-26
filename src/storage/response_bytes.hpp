#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace trading {
// Lossless ASCII encoding retains malformed/non-UTF-8 HTTP bodies before JSON
// decoding. Metadata and response bytes commit in one atomic artifact.
[[nodiscard]] inline std::string encode_response_bytes(std::string_view bytes) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto value : bytes) {
        const auto byte = static_cast<unsigned char>(value);
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}
[[nodiscard]] inline std::string decode_response_bytes(std::string_view encoded) {
    if (encoded.size() % 2 != 0 || encoded.size() > 2U * 65536U) { throw std::invalid_argument("invalid archived response-byte size"); }
    const auto digit = [](char value) -> unsigned {
        if (value >= '0' && value <= '9') { return static_cast<unsigned>(value - '0'); }
        if (value >= 'a' && value <= 'f') { return static_cast<unsigned>(value - 'a' + 10); }
        throw std::invalid_argument("invalid archived response-byte encoding");
    };
    std::string result;
    result.reserve(encoded.size() / 2);
    for (std::size_t i = 0; i < encoded.size(); i += 2) {
        result.push_back(static_cast<char>((digit(encoded[i]) << 4) | digit(encoded[i + 1])));
    }
    return result;
}
} // namespace trading
