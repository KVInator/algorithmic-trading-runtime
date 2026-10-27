#include "panel_input_policy.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
PanelInputPolicy::PanelInputPolicy(nlohmann::json settings, std::string identity, ExtensionStudyContract parent)
    : settings_(std::move(settings)), identity_{std::move(identity)}, parent_{std::move(parent)} {
    std::set<std::size_t> slots{settings_.at("mandatory_open_minute").get<std::size_t>()};
    const auto delays = settings_.at("execution_delays_minutes").get<std::vector<std::size_t>>();
    const auto add = [&](std::size_t decision) {
        for (const auto delay : delays) {
            if (decision + delay >= settings_.at("session_minutes").get<std::size_t>()) {
                throw std::invalid_argument("registered execution reference lies outside the session");
            }
            slots.insert(decision + delay);
        }
    };
    add(settings_.at("momentum").at("decision_minute").get<std::size_t>());
    for (const auto& minute : settings_.at("momentum").at("exit_decision_minutes")) { add(minute.get<std::size_t>()); }
    for (const auto& minute : settings_.at("continuation").at("decision_minutes")) { add(minute.get<std::size_t>()); }
    add(settings_.at("continuation").at("liquidation_minute").get<std::size_t>());
    const auto reversion = settings_.at("reversion").at("decision_minute").get<std::size_t>();
    add(reversion);
    for (const auto& holding : settings_.at("reversion").at("holding_minutes")) { add(reversion + holding.get<std::size_t>()); }
    execution_slots_.assign(slots.begin(), slots.end());
}

PanelInputPolicy PanelInputPolicy::load(const std::filesystem::path& file, const ExtensionStudyContract& parent) {
    const auto text = read_text_file(file, 64U * 1024U);
    const auto identity = sha256(text);
    if (identity != TRADING_EXTENSION_INPUT_POLICY_SHA256) {
        throw std::invalid_argument("input policy differs from its registered build identity");
    }
    auto settings = parse_json_document(text);
    if (settings.at("schema_version").get<int>() != 1 || settings.at("parent_protocol_sha256").get<std::string>() != parent.identity()) {
        throw std::invalid_argument("input amendment parent identity mismatch");
    }
    return {std::move(settings), identity, parent};
}

const std::string& PanelInputPolicy::identity() const noexcept { return identity_; }
const std::vector<std::size_t>& PanelInputPolicy::execution_slots() const noexcept { return execution_slots_; }
const nlohmann::json& PanelInputPolicy::closing_recovery_plan() const noexcept { return settings_.at("closing_recovery_plan"); }

PanelInputAvailability PanelInputPolicy::evaluate(const CalendarSession& session,
    std::string_view symbol, std::span<const std::chrono::sys_seconds> missing, std::size_t reversion_observation_step_minutes) const {
    const auto symbols = parent_.symbols();
    if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) { throw std::invalid_argument("input policy symbol is outside the registered universe"); }
    const auto expected = session.bar_request(std::string{symbol}).expected_minutes();
    std::array<bool, 390> observed{};
    if (expected > observed.size()) { throw std::invalid_argument("input policy session exceeds the registered regular grid"); }
    std::fill_n(observed.begin(), expected, true);
    std::optional<std::chrono::sys_seconds> previous;
    for (const auto at : missing) {
        const auto seconds = (at - session.open_utc()).count();
        if (seconds < 0 || at >= session.close_utc() || seconds % 60 != 0 || (previous && at <= *previous)) {
            throw std::invalid_argument("input policy requires sorted unique missing UTC minute slots");
        }
        observed.at(static_cast<std::size_t>(seconds / 60)) = false;
        previous = at;
    }
    PanelInputAvailability result;
    result.regular_session = session.regular();
    result.diagnostic_excluded = parent_.diagnostic_excluded(symbol, session.date());
    const auto& halt_dates = settings_.at("halt_context_dates");
    result.known_halt_date = std::any_of(halt_dates.begin(), halt_dates.end(), [&](const auto& day) {
        return day.template get<std::string>() == session.date();
    });
    for (const auto slot : execution_slots_) { if (!observed.at(slot)) { result.missing_execution_slots.push_back(slot); } }
    result.operational = result.regular_session && !result.diagnostic_excluded && result.missing_execution_slots.empty();
    result.momentum_features = std::all_of(settings_.at("momentum").at("feature_minute_slots").begin(),
        settings_.at("momentum").at("feature_minute_slots").end(), [&](const auto& slot) { return observed.at(slot.template get<std::size_t>()); });
    const auto anchor = settings_.at("reversion").at("anchor_minutes").get<std::size_t>();
    result.reversion_features = std::all_of(observed.begin(), observed.begin() + static_cast<std::ptrdiff_t>(anchor), [](bool value) { return value; });
    const auto end = settings_.at("reversion").at("decision_minute").get<std::size_t>();
    const auto& allowed = settings_.at("reversion").at("allowed_observation_step_minutes");
    if (std::none_of(allowed.begin(), allowed.end(), [&](const auto& value) { return value.template get<std::size_t>() == reversion_observation_step_minutes; })) {
        throw std::invalid_argument("reversion observation step is outside the registered neighborhood");
    }
    const auto step = reversion_observation_step_minutes;
    for (auto slot = anchor - 1; slot < end; slot += step) { result.reversion_features = result.reversion_features && observed.at(slot); }
    for (const auto& decision : settings_.at("continuation").at("decision_minutes")) {
        const auto minutes = decision.get<std::ptrdiff_t>();
        result.continuation_prefixes.push_back(std::all_of(observed.begin(), observed.begin() + minutes, [](bool value) { return value; }));
    }
    return result;
}
} // namespace trading
