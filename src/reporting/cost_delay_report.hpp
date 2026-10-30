#pragma once
#include "cost_delay_study.hpp"
#include <fstream>
#include <map>
#include <tuple>

namespace trading {
class CostDelayReport {
  public:
    CostDelayReport(const std::filesystem::path &run_root, nlohmann::json provenance,
                    std::filesystem::path input_dataset, CostDelayStudy study,
                    PanelEvaluationAccess access = {});
    [[nodiscard]] const std::filesystem::path &directory() const noexcept;
    void append(std::span<const PanelReplayDay> days, std::span<const SessionDownload> sessions);
    [[nodiscard]] std::filesystem::path finish(nlohmann::json checks = nlohmann::json::array());

  private:
    struct Series {
        std::vector<double> values;
        std::size_t active{}, trades{};
        double turnover{}, cost_paid{}, minutes_held{};
    };
    struct Output {
        std::ofstream stream;
        std::size_t bytes{}, records{}, scored{}, common{};
    };
    using Key = std::tuple<std::string, std::string, int, double>;
    std::filesystem::path directory_;
    CostDelayStudy study_;
    nlohmann::json summary_;
    std::map<std::string, Output> outputs_;
    std::map<Key, Series> series_, pairs_;
    std::map<std::string, std::pair<std::size_t, std::size_t>> coverage_;
    std::string previous_date_;
    bool finished_{};
};
} // namespace trading
