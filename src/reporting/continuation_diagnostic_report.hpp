#pragma once

#include "intraday_continuation.hpp"

#include <string>

namespace trading {

// These renderers are explicitly for the prescribed synthetic contract fixture.
[[nodiscard]] std::string format_continuation_diagnostic_html(
    const HistoricalBarRequest& session,
    const IntradayContinuationReplay& price_only,
    const IntradayContinuationReplay& volume_conditioned);

[[nodiscard]] std::string format_continuation_diagnostic_csv(
    const IntradayContinuationReplay& price_only,
    const IntradayContinuationReplay& volume_conditioned);

} // namespace trading
