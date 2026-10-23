#pragma once

#include "intraday_continuation.hpp"

#include <string>

namespace trading {

// The two synthetic replays must outlive the report.
class ContinuationDiagnosticReport {
public:
    ContinuationDiagnosticReport(const IntradayContinuationReplay& price,
        const IntradayContinuationReplay& volume) : price_{price}, volume_{volume} {}
    [[nodiscard]] std::string html(const HistoricalBarRequest& session) const;
    [[nodiscard]] std::string csv() const;

private:
    const IntradayContinuationReplay& price_;
    const IntradayContinuationReplay& volume_;
};

} // namespace trading
