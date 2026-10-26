#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace trading {
inline void validate_equity_symbol(std::string_view symbol) {
    if (symbol.empty() || symbol.size() > 32 ||
        !std::all_of(symbol.begin(), symbol.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        })) {
        throw std::invalid_argument("request requires a single uppercase equity symbol (A-Z, 0-9, dot, hyphen)");
    }
}

inline std::string encode_query_value(std::string_view value) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15U];
        }
    }
    return result;
}

inline std::string page_token_query(const std::optional<std::string>& token) {
    if (!token) { return {}; }
    if (token->empty() || token->size() > 4096) {
        throw std::invalid_argument("pagination token must contain 1 to 4096 bytes");
    }
    return "&page_token=" + encode_query_value(*token);
}
} // namespace trading
