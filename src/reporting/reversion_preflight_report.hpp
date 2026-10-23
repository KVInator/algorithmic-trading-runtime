#pragma once

#include "reversion_preflight.hpp"

#include <filesystem>

namespace trading {
void write_reversion_preflight_report(const std::filesystem::path& directory, const ReversionPreflight& result);
} // namespace trading
