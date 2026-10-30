#pragma once
#include "extension_contract.hpp"
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace trading {
class PriorMarketCondition {
  public:
    [[nodiscard]] nlohmann::json advance(const std::string &date, std::optional<double> current);

  private:
    std::deque<std::optional<double>> returns_, cutoffs_;
    std::string previous_;
};
struct StabilityObservation {
    std::optional<double> net, gross, exposure, turnover, cost, holding_minutes, trades;
    std::string condition{"unavailable"};
};
struct StabilitySeries {
    std::string id, layer, symbol, policy;
    std::vector<StabilityObservation> days;
};
struct StabilityPair {
    double candidate, control;
};
struct StabilityEndpoint {
    std::string id;
    bool cer{};
    double material{};
    std::vector<std::optional<StabilityPair>> days;
};
struct StabilityInputs {
    std::vector<std::string> dates;
    std::vector<StabilitySeries> series;
    std::vector<StabilityEndpoint> endpoints;
    nlohmann::json market, sources;
    bool synthetic{};
};

// Immutable sampling indices only; each trial still estimates its own moments.
class InferenceDrawSchedule {
  public:
    InferenceDrawSchedule(std::size_t dates, std::size_t block, std::size_t replicates,
                          std::uint64_t seed);
    [[nodiscard]] std::size_t dates() const noexcept { return dates_; }
    [[nodiscard]] std::size_t block() const noexcept { return block_; }
    [[nodiscard]] std::size_t replicates() const noexcept { return draws_.size(); }
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
    [[nodiscard]] const std::string &identity() const noexcept { return identity_; }
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>> &draw(std::size_t i) const {
        return draws_.at(i);
    }

  private:
    std::size_t dates_, block_;
    std::uint64_t seed_;
    std::string identity_;
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> draws_;
};

// Fixed original-sample calendar-indexed influence functions studentize joint
// non-circular block draws. Missing observations never become valid zero returns.
class FullCalendarInference {
  public:
    explicit FullCalendarInference(const ExtensionStudyContract &contract);
    [[nodiscard]] nlohmann::json run(const std::vector<StabilityEndpoint> &endpoints,
                                     std::size_t block_length) const;
    [[nodiscard]] nlohmann::json run(const std::vector<StabilityEndpoint> &endpoints,
                                     std::size_t block_length,
                                     const InferenceDrawSchedule *schedule) const;

  private:
    nlohmann::json settings_;
    nlohmann::json definitions_;
};

class StabilityStudy {
  public:
    using Progress = std::function<void(const std::string &)>;
    explicit StabilityStudy(const ExtensionStudyContract &contract);
    [[nodiscard]] nlohmann::json plan() const;
    [[nodiscard]] nlohmann::json run(const StabilityInputs &inputs, const Progress &progress = {}) const;
    [[nodiscard]] nlohmann::json numerical_checks() const;
    [[nodiscard]] static nlohmann::json describe(const StabilityInputs &inputs);

  private:
    ExtensionStudyContract contract_;
};
[[nodiscard]] nlohmann::json stability_observation_json(const StabilityObservation &observation);
[[nodiscard]] nlohmann::json stability_endpoint_json(const StabilityEndpoint &endpoint);
} // namespace trading
