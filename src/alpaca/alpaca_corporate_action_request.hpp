#pragma once

#include <string>

namespace trading {

// One-page, all-type SPY capability request. Not an ex-date coverage contract.
class CorporateActionProbeRequest {
public:
    explicit CorporateActionProbeRequest(int process_date_year);
    [[nodiscard]] std::string path() const;
private:
    int year_;
};

} // namespace trading
