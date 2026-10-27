#pragma once

#include "extension_contract.hpp"
#include "market_calendar.hpp"
#include <chrono>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace trading {

// These flags describe observed inputs, never model admissibility or activation.
struct PanelInputAvailability {
    bool regular_session{};
    bool diagnostic_excluded{};
    bool operational{};
    bool momentum_features{};
    bool reversion_features{};
    bool known_halt_date{};
    std::vector<bool> continuation_prefixes;
    std::vector<std::size_t> missing_execution_slots;
};

// Immutable amendment separates execution observability from feature availability.
// It consumes missing timestamps without interpolating bars or changing clocks.
class PanelInputPolicy {
public:
    [[nodiscard]] static PanelInputPolicy load(const std::filesystem::path& file,
        const ExtensionStudyContract& parent);
    [[nodiscard]] const std::string& identity() const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& execution_slots() const noexcept;
    [[nodiscard]] const nlohmann::json& closing_recovery_plan() const noexcept;
    [[nodiscard]] PanelInputAvailability evaluate(const CalendarSession& session,
        std::string_view symbol, std::span<const std::chrono::sys_seconds> missing,
        std::size_t reversion_observation_step_minutes = 5) const;
private:
    PanelInputPolicy(nlohmann::json settings, std::string identity, ExtensionStudyContract parent);
    nlohmann::json settings_;
    std::string identity_;
    ExtensionStudyContract parent_;
    std::vector<std::size_t> execution_slots_;
};
// Registered source precedence and bounded missing-mark treatment, separate from
// the immutable availability amendment and original acquisition.
class PanelSourceResolution {
public:
    [[nodiscard]] static PanelSourceResolution load(const std::filesystem::path& file,
        const PanelInputPolicy& parent);
    [[nodiscard]] const std::string& identity() const noexcept;
    [[nodiscard]] const nlohmann::json& document() const noexcept;
    [[nodiscard]] bool permits_missing_closes(const nlohmann::json& remaining,
        const nlohmann::json& recovery) const;
private:
    PanelSourceResolution(nlohmann::json settings, std::string identity);
    nlohmann::json settings_;
    std::string identity_;
};
} // namespace trading
