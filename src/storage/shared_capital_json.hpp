#pragma once
#include "shared_capital.hpp"

namespace trading::execution {
[[nodiscard]] nlohmann::json shared_configuration_json(const SharedCapitalConfiguration& configuration);
[[nodiscard]] nlohmann::json shared_input_json(const SharedInput& input);
[[nodiscard]] SharedInput parse_shared_input(const nlohmann::json& value);
[[nodiscard]] nlohmann::json shared_result_json(const SharedResult& result);
[[nodiscard]] nlohmann::json shared_snapshot_json(const SharedCapitalSnapshot& snapshot);
[[nodiscard]] nlohmann::json shared_checkpoint_json(const SharedFlatCheckpoint& checkpoint);
[[nodiscard]] SharedFlatCheckpoint parse_shared_checkpoint(const nlohmann::json& value);
} // namespace trading::execution
