#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace trading {

// One parser for provider responses and local provenance documents.
[[nodiscard]] nlohmann::json parse_json_document(std::string_view payload);

} // namespace trading
