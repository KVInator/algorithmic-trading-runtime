#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

// The registered JSON is the numerical authority. Loading checks its build-pinned
// identity; constructors and accessors perform no acquisition or scoring.
class EvaluationAuthorization;
class PanelEvaluationAccess {
  public:
    PanelEvaluationAccess() = default;
    [[nodiscard]] bool development() const noexcept { return year_ == 2023; }
    [[nodiscard]] int year() const noexcept { return year_; }
    [[nodiscard]] bool scored(std::string_view date) const;
    [[nodiscard]] bool unit_active(std::string_view date) const;
    [[nodiscard]] std::string first_input() const;
    [[nodiscard]] std::string last_input() const;
    [[nodiscard]] const std::string &unit_first() const noexcept { return unit_first_; }
    [[nodiscard]] const std::string &authority() const noexcept { return authority_; }
    [[nodiscard]] bool calibration_size_excess() const noexcept { return size_excess_; }
    void require_price(std::string_view date) const;
    void require_origin(bool provider) const;
    [[nodiscard]] nlohmann::json document() const;

  private:
    friend class EvaluationAuthorization;
    PanelEvaluationAccess(int year, bool provider, std::string authority, bool excess);
    int year_{2023};
    bool provider_{true}, size_excess_{};
    std::string unit_first_{"2016-01-01"}, authority_;
};
class ExtensionStudyContract {
  public:
    [[nodiscard]] static ExtensionStudyContract load(const std::filesystem::path &path);
    [[nodiscard]] const nlohmann::json &document() const noexcept;
    [[nodiscard]] const std::string &identity() const noexcept;
    [[nodiscard]] std::vector<std::string> symbols() const;
    [[nodiscard]] bool reserved(std::string_view date) const;
    [[nodiscard]] bool diagnostic_excluded(std::string_view symbol, std::string_view date) const;
    void require_development_access(std::string_view date) const;

  private:
    ExtensionStudyContract(nlohmann::json document, std::string identity);
    nlohmann::json document_;
    std::string identity_;
};

} // namespace trading
