#include "strategy_execution_application.hpp"

int main() {
    try {
        trading::execution::StrategyExecutionApplication{TRADING_PROJECT_ROOT}.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Strategy reference replay failed: " << e.what() << '\n';
        return 1;
    }
}
