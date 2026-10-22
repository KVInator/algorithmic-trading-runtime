#pragma once

#include "minute_bar.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

struct AlpacaBarPage {
    std::vector<MinuteBar> bars;
    std::optional<std::string> next_page_token;
};

// Decode one ascending, single-symbol response page. Missing minutes are not
// filled, and the opaque pagination token is preserved without modification.
[[nodiscard]] AlpacaBarPage decode_alpaca_bar_page(std::string_view payload,
                                                 std::string_view expected_symbol);

} // namespace trading
