#pragma once

#include "alpaca_credentials.hpp"

// Uses the saved Windows credential, or hidden input when no entry exists.
[[nodiscard]] trading::AlpacaCredentials load_alpaca_console_credentials();
