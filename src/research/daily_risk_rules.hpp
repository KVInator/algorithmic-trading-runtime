#pragma once

#include "market_calendar.hpp"

#include <array>
#include <string_view>

namespace trading::risk_rules {
[[nodiscard]] std::vector<CalendarSession> calibration_calendar();
[[nodiscard]] std::vector<CalendarSession> development_calendar();
inline constexpr std::size_t history = 20;
inline constexpr std::size_t calibration_sessions = 252;
inline constexpr std::size_t development_sessions = 501;
inline constexpr double initial_cash = 100000;
inline constexpr std::array<double, 5> costs{0, .5, 1, 2, 5};
inline constexpr std::array<int, 2> delays{1, 2};
inline constexpr std::array<std::string_view, 4> accounts{"intraday_risk", "daily_only", "static_risk_budget", "passive_equity"};
inline constexpr std::string_view registration_commit = "c75ff8aad733e60318ae11803c940b3f63934a19";
inline constexpr std::string_view protocol_identity =
    "28e935174043745098b3f53d7eb71669fe54c8fc6d510a68c568da28874c8a3a:"
    "3a37f24f29e661c606c32af81b3daf5b5e909520aaaeaf5eafe7efd0340f51d2";
inline constexpr std::string_view development_digest = "70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e";
inline constexpr std::string_view acquisition_digest = "d1f500c927480e7a7013df86e476785b66afdfa5d54c2f97e757957f34ce2808";
// Canonical 2023-01-03 bars in the completed acquisition coverage audit.
inline constexpr std::string_view diagnostic_digest = "f6c39551835e90f48c2166a572ea52653038f11ce3597337dcb563e4a0034e34";
inline constexpr std::string_view issuer_digest = "d753b529129a27a9db03dff41f36b52557ce558e90f5dc5f8ca0f1ccd25da66a";
} // namespace trading::risk_rules
