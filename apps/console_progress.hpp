#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace trading {

class ConsoleProgress {
public:
    void operator()(const std::string& message) const {
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        std::ostringstream line;
        line << '[' << std::fixed << std::setprecision(1) << elapsed << "s] " << message;
        std::cout << line.str() << std::endl;
    }
private:
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
};

} // namespace trading
