#include "runtime_input.hpp"

#include <type_traits>

namespace trading::execution {

Outcome apply_input(OrderRuntime& runtime, const RuntimeInput& value) {
    return std::visit([&](const auto& input) -> Outcome {
        using T = std::decay_t<decltype(input)>;
        if constexpr (std::is_same_v<T, SubmitInput>) { return runtime.submit(input.request); }
        else if constexpr (std::is_same_v<T, CancelInput>) { return runtime.request_cancel(input.order_id, input.cancel_id); }
        else if constexpr (std::is_same_v<T, BrokerReport>) { return runtime.receive(input); }
        else if constexpr (std::is_same_v<T, BrokerSnapshot>) { return runtime.reconcile(input); }
        else if constexpr (std::is_same_v<T, TargetIntent>) { return runtime.register_target(input); }
        else {
            if (input.connected) { runtime.reconnect(); } else { runtime.disconnect(); }
            return Outcome::applied;
        }
    }, value);
}

} // namespace trading::execution
