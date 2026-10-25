#pragma once
#include "paper_transport.hpp"

namespace trading {
struct PaperTransportResult {
    std::vector<std::string> checks;
    nlohmann::json cases = nlohmann::json::array();
    bool successful{true};
    std::string detail;
};

} // namespace trading
