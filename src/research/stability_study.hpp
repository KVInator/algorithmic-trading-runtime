#pragma once
#include "extension_contract.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace trading {
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

// Fixed original-sample calendar-indexed influence functions studentize joint
// non-circular block draws. Missing observations never become valid zero returns.
class FullCalendarInference {
  public:
    explicit FullCalendarInference(const ExtensionStudyContract &contract);
    [[nodiscard]] nlohmann::json run(const std::vector<StabilityEndpoint> &endpoints,
                                     std::size_t block_length) const;

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

  private:
    ExtensionStudyContract contract_;
};
[[nodiscard]] nlohmann::json stability_observation_json(const StabilityObservation &observation);
[[nodiscard]] nlohmann::json stability_endpoint_json(const StabilityEndpoint &endpoint);
} // namespace trading
