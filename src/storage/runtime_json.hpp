#pragma once

#include "runtime_input.hpp"

#include <nlohmann/json_fwd.hpp>

namespace trading::execution {

[[nodiscard]] nlohmann::json input_json(const RuntimeInput& input);
[[nodiscard]] RuntimeInput parse_runtime_input(const nlohmann::json& value);
[[nodiscard]] nlohmann::json account_json(const AccountSnapshot& account);
[[nodiscard]] nlohmann::json trace_json(const TraceEntry& trace);

} // namespace trading::execution
