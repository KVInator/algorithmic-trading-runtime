#pragma once

#include "journal_storage.hpp"
#include <nlohmann/json.hpp>
#include <utility>

namespace trading::execution {
inline constexpr std::size_t maximum_frame_bytes = 64U * 1024U;
inline constexpr std::size_t maximum_input_records = 10000;

// Shared by legacy and paper journals; semantic replay remains model-specific.
class JournalFrame {
public:
    [[nodiscard]] static std::pair<std::string, std::string> encode(const nlohmann::json& value);
    // A complete line without its LF terminator.
    [[nodiscard]] static std::pair<nlohmann::json, std::string> decode(std::string_view line);
};
} // namespace trading::execution
