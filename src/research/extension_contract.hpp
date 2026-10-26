#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

// The registered JSON is the numerical authority. Loading checks its build-pinned
// identity; constructors and accessors perform no acquisition or scoring.
class ExtensionStudyContract {
public:
    [[nodiscard]] static ExtensionStudyContract load(const std::filesystem::path& path);
    [[nodiscard]] const nlohmann::json& document() const noexcept;
    [[nodiscard]] const std::string& identity() const noexcept;
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
