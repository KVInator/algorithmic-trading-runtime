#pragma once

#include "reversion_preflight.hpp"

#include <filesystem>

namespace trading {

// The result must outlive the report; export does not modify its inputs.
class ReversionPreflightReport {
public:
    explicit ReversionPreflightReport(const ReversionPreflightResult& result) : result_{result} {}
    void write(const std::filesystem::path& directory) const;

private:
    const ReversionPreflightResult& result_;
};

} // namespace trading
