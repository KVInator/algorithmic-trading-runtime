#pragma once

#include "reversion_validation.hpp"

#include <filesystem>

namespace trading {
class ReversionValidationReport {
public:
    explicit ReversionValidationReport(const ReversionValidationResult& result) : result_{result} {}
    void write(const std::filesystem::path& directory) const;
private:
    const ReversionValidationResult& result_;
};
} // namespace trading
