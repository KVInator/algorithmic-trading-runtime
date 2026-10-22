#pragma once

#include "session_download.hpp"

#include <filesystem>
#include <functional>

namespace trading {

struct GapProbeResult {
    std::filesystem::path directory;
    std::size_t probe_windows;
    std::size_t missing_minutes;
    std::size_t candidate_minutes_found;
    std::size_t conflicting_minutes;
};

// Diagnostic evidence only. Queries each missing minute and each contiguous
// gap with one neighbouring minute on either side, clipped to the source
// window. A maximum of 16 planned windows bounds one invocation.
// The source download, dataset checkpoints and manifests are never modified.
class SessionGapProbe {
public:
    using Progress = std::function<void(const std::string&)>;
    explicit SessionGapProbe(std::filesystem::path source_directory);
    [[nodiscard]] GapProbeResult run(const std::filesystem::path& output_root,
                                      const BarPageFetcher& fetch_page,
                                      const Progress& progress = {}) const;
private:
    std::filesystem::path source_directory_;
};

} // namespace trading
