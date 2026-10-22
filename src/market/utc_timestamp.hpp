#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace trading {

// Whole-second UTC timestamps in YYYY-MM-DDTHH:MM:SS[.000...]Z form.
// Non-zero fractional seconds and timezone offsets are rejected, not rounded.
[[nodiscard]] std::chrono::sys_seconds parse_utc_seconds(std::string_view value);
[[nodiscard]] std::string format_utc_seconds(std::chrono::sys_seconds value);

} // namespace trading
