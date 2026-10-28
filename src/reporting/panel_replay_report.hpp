#pragma once
#include "panel_replay.hpp"
#include <fstream>
#include <map>

namespace trading {
class PanelReplayReport {
public:
    PanelReplayReport(const std::filesystem::path& run_root, nlohmann::json provenance,
        nlohmann::json parity, std::filesystem::path input_dataset = {});
    void append(std::vector<PanelReplayDay> day);
    [[nodiscard]] std::filesystem::path finish();
private:
    std::filesystem::path directory_;
    nlohmann::json summary_, samples_ = nlohmann::json::array();
    struct Output { std::ofstream stream; std::size_t bytes{}, records{}, scored{}, common{}, risk_ready{}; };
    std::map<std::string, Output> outputs_;
    bool finished_{};
};
} // namespace trading
