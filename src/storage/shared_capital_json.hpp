#pragma once
#include "shared_capital.hpp"

namespace trading::execution {
[[nodiscard]] nlohmann::json broker_order_json(const OrderRequest& order);
[[nodiscard]] OrderRequest parse_broker_order(const nlohmann::json& value);
[[nodiscard]] nlohmann::json broker_report_json(const BrokerReport& report);
[[nodiscard]] BrokerReport parse_broker_report(const nlohmann::json& value);
[[nodiscard]] nlohmann::json shared_configuration_json(const SharedCapitalConfiguration& configuration);
[[nodiscard]] nlohmann::json shared_input_json(const SharedInput& input);
[[nodiscard]] SharedInput parse_shared_input(const nlohmann::json& value);
[[nodiscard]] nlohmann::json shared_result_json(const SharedResult& result);
[[nodiscard]] nlohmann::json shared_snapshot_json(const SharedCapitalSnapshot& snapshot);
[[nodiscard]] nlohmann::json shared_checkpoint_json(const SharedFlatCheckpoint& checkpoint);
[[nodiscard]] SharedFlatCheckpoint parse_shared_checkpoint(const nlohmann::json& value);
} // namespace trading::execution
