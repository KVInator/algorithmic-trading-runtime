#pragma once
#include "session_download.hpp"
#include <optional>

namespace trading {
enum class ResearchInputMode { legacy_complete, registered_panel };

// A non-owning view of validated, ordered observations. Missing minutes retain
// their clock positions. The caller owns the bars for the lifetime of the view.
class ResearchSessionView {
public:
    ResearchSessionView(const HistoricalBarRequest& request, std::span<const MinuteBar> bars);
    [[nodiscard]] const MinuteBar* at(std::size_t minute) const;
    [[nodiscard]] std::span<const MinuteBar> before(std::chrono::sys_seconds cutoff) const;
private:
    HistoricalBarRequest request_;
    std::span<const MinuteBar> bars_;
};
void require_panel_regular_session(const HistoricalBarRequest& request);
} // namespace trading
