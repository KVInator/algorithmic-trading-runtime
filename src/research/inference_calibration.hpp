#pragma once
#include "stability_study.hpp"
#include <array>
namespace trading {
class ValidationProtocol {
  public:
    [[nodiscard]] static ValidationProtocol load(const std::filesystem::path &path);
    [[nodiscard]] const nlohmann::json &document() const noexcept { return document_; }
    [[nodiscard]] const std::string &identity() const noexcept { return identity_; }

  private:
    ValidationProtocol(nlohmann::json d, std::string id)
        : document_(std::move(d)), identity_(std::move(id)) {}
    nlohmann::json document_;
    std::string identity_;
};
struct InferenceCalibrationCase {
    std::string id;
    double noise_basis_points{}, ar1{};
    bool missing{};
};
class InferenceCalibration {
  public:
    InferenceCalibration(ExtensionStudyContract c, ValidationProtocol p)
        : contract_(std::move(c)), protocol_(std::move(p)) {}
    [[nodiscard]] std::vector<InferenceCalibrationCase> cases() const;
    [[nodiscard]] std::vector<StabilityEndpoint> sample(const InferenceCalibrationCase &c,
                                                        std::size_t trial) const;
    [[nodiscard]] std::array<double, 6> alternative_shifts(const InferenceCalibrationCase &c) const;
    [[nodiscard]] nlohmann::json trial_record(const InferenceCalibrationCase &c, std::size_t trial,
                                              const nlohmann::json &result) const;
    [[nodiscard]] nlohmann::json aggregate(const InferenceCalibrationCase &c, std::size_t block,
                                           const std::vector<nlohmann::json> &records) const;
    [[nodiscard]] nlohmann::json numerical_checks() const;

  private:
    ExtensionStudyContract contract_;
    ValidationProtocol protocol_;
};
[[nodiscard]] nlohmann::json binomial_diagnostic(std::size_t events, std::size_t trials, double p,
                                                 double z);
} // namespace trading
