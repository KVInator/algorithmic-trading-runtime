#pragma once

#include "reversion_study.hpp"

namespace trading {
class ReversionStudyReport {
public:
    explicit ReversionStudyReport(const ReversionStudyResult& result) : result_{result} {}

    void write(const std::filesystem::path& directory,
               std::optional<std::string> verified_manifest = std::nullopt) const;

private:
    const ReversionStudyResult& result_;
};
} // namespace trading
