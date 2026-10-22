#pragma once

#include "minute_bar.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

// The project's single-session, unquoted seven-column CSV schema. Parsing
// validates each value; chronological/session coverage belongs to the audit.
[[nodiscard]] std::string format_minute_bars_csv(std::span<const MinuteBar> bars);
[[nodiscard]] std::vector<MinuteBar> parse_minute_bars_csv(std::string_view payload);

} // namespace trading
