#include "strategy_execution_application.hpp"

int main(int argc, char** argv) {
    try {
        const bool historical = argc == 2 && std::string_view{argv[1]} == "--historical";
        if (argc > 2 || (argc == 2 && !historical)) {
            throw std::invalid_argument("supported option: --historical");
        }
        trading::execution::StrategyExecutionApplication{TRADING_PROJECT_ROOT}.run(historical);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Strategy execution diagnostic failed: " << e.what() << '\n';
        return 1;
    }
}
