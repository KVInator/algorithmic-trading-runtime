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

[[nodiscard]] std::string format_continuation_daily_csv(const ContinuationStudyResult& study);
[[nodiscard]] std::string format_continuation_features_csv(const ContinuationStudyResult& study);
[[nodiscard]] std::string format_continuation_trades_csv(const ContinuationStudyResult& study);
[[nodiscard]] std::string format_removed_exposure_csv(const ContinuationDiagnosticsResult& diagnostics);
[[nodiscard]] std::string format_opportunity_diagnostics_csv(const ContinuationDiagnosticsResult& diagnostics);
[[nodiscard]] std::string format_entry_timing_csv(const ContinuationDiagnosticsResult& diagnostics);
[[nodiscard]] std::string_view continuation_release_calendar_sha256() noexcept;
[[nodiscard]] std::string format_continuation_study_html(
    const ContinuationStudyResult& study, const ContinuationReportContext& context,
    const ContinuationDiagnosticsResult* diagnostics = nullptr);
[[nodiscard]] std::filesystem::path save_continuation_report(
    const FrozenDevelopmentDataset& dataset, const ContinuationStudyResult& study,
    const ContinuationDiagnosticsResult& diagnostics, std::string_view release_calendar_csv,
    const std::filesystem::path& output_root);

} // namespace trading
