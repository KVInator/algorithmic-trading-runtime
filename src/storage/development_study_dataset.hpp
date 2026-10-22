#pragma once

#include "development_dataset.hpp"

#include <optional>
#include <functional>

namespace trading {

// Read-only verification of collected data followed by immutable publication
// of study_manifest.json. The protocol and data policy are checked before any
// performance calculation; source artifacts are never rewritten.
class DevelopmentStudyDataset {
public:
    using Progress = std::function<void(const std::string&)>;
    explicit DevelopmentStudyDataset(std::filesystem::path project_root,
        std::optional<std::string> expected_policy_sha256 = std::nullopt);
    [[nodiscard]] FrozenDevelopmentDataset finalize(const Progress& progress = {}) const;
    [[nodiscard]] FrozenDevelopmentDataset load(const Progress& progress = {}) const;
private:
    struct VerifiedInput;
    [[nodiscard]] VerifiedInput verify(const Progress& progress) const;
    std::filesystem::path project_root_;
    std::string expected_policy_sha256_;
};

} // namespace trading
