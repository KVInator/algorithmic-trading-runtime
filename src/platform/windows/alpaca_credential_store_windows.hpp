#pragma once

#include "alpaca_credentials.hpp"

#include <optional>

namespace trading {

// Reads only the named generic credential for the current Windows user.
// Absence returns nullopt; malformed entries and access errors are reported.
[[nodiscard]] std::optional<AlpacaCredentials> load_saved_alpaca_paper_credentials();

} // namespace trading
