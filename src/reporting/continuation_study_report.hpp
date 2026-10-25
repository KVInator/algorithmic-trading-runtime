#pragma once

#include "continuation_diagnostics.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace trading {

struct ContinuationReportContext {
    std::string data_kind; // development or synthetic_contract
    std::string input_manifest_sha256;
    std::string features_sha256;
};

// Bound results must outlive their report. Rendering never changes study values.
class ContinuationStudyReport {
public:
    explicit ContinuationStudyReport(const ContinuationStudyResult& study) : study_{study} {}
    [[nodiscard]] std::string daily_csv() const;
    [[nodiscard]] std::string features_csv() const;
    [[nodiscard]] std::string trades_csv() const;
    [[nodiscard]] std::string html(const ContinuationReportContext& context,
        const ContinuationDiagnosticsResult* diagnostics = nullptr) const;
    [[nodiscard]] std::filesystem::path write(const FrozenDevelopmentDataset& dataset,
        const ContinuationDiagnosticsResult& diagnostics, std::string_view release_calendar_csv,
        const std::filesystem::path& output_root) const;
    [[nodiscard]] static std::string_view release_calendar_sha256() noexcept;

private:
    const ContinuationStudyResult& study_;
};

class ContinuationDiagnosticsReport {
public:
    explicit ContinuationDiagnosticsReport(const ContinuationDiagnosticsResult& diagnostics) : diagnostics_{diagnostics} {}
    [[nodiscard]] std::string removed_exposure_csv() const;
    [[nodiscard]] std::string opportunities_csv() const;
    [[nodiscard]] std::string entry_timing_csv() const;

private:
    const ContinuationDiagnosticsResult& diagnostics_;
};

} // namespace trading
