#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace trading {

// One parser for provider responses and local provenance documents.
[[nodiscard]] nlohmann::json parse_json_document(std::string_view payload);

// Validate through the same parser, then preserve every numeric lexeme as text.
// Consumers still validate provider field types and the supported decimal syntax.
[[nodiscard]] nlohmann::json parse_json_number_text(std::string_view payload);

} // namespace trading
