#pragma once

#include "order_runtime.hpp"

#include <variant>

namespace trading::execution {

struct SubmitInput { OrderRequest request; };
struct CancelInput { std::string order_id; std::string cancel_id; };
struct ConnectionInput { bool connected; };
using RuntimeInput = std::variant<SubmitInput, CancelInput, BrokerReport, ConnectionInput, BrokerSnapshot, TargetIntent>;

[[nodiscard]] Outcome apply_input(OrderRuntime& runtime, const RuntimeInput& input);

} // namespace trading::execution
