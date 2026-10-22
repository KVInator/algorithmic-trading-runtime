#pragma once

#include "continuation_study.hpp"

#include <filesystem>
#include <string>

namespace trading {

struct ContinuationReportContext {
    std::string data_kind; // development or synthetic_contract
    std::string input_manifest_sha256;
    std::string features_sha256;
};

[[nodiscard]] std::string format_continuation_daily_csv(const ContinuationStudyResult& study);
[[nodiscard]] std::string format_continuation_features_csv(const ContinuationStudyResult& study);
[[nodiscard]] std::string format_continuation_trades_csv(const ContinuationStudyResult& study);
[[nodiscard]] std::string format_continuation_study_html(
    const ContinuationStudyResult& study, const ContinuationReportContext& context);
[[nodiscard]] std::filesystem::path save_continuation_report(
    const FrozenDevelopmentDataset& dataset, const ContinuationStudyResult& study,
    const std::filesystem::path& output_root);

} // namespace trading
