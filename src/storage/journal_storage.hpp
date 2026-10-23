#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace trading::execution {

inline constexpr std::size_t maximum_journal_bytes = 32U * 1024U * 1024U;

// An exclusively owned, single-writer append target. A successful append must
// synchronize bytes through the platform file API before returning. Tests may
// substitute faults at this boundary; callers must not retry an uncertain write.
class JournalStorage {
public:
    virtual ~JournalStorage() = default;
    [[nodiscard]] virtual std::string read_all() = 0;
    virtual void append_and_sync(std::string_view bytes) = 0;
};

// Parents must exist. Creation never replaces a file; opening never truncates.
// Both factories retain an exclusive writer lock until destruction.
[[nodiscard]] std::unique_ptr<JournalStorage> create_journal_file(const std::filesystem::path& path);
[[nodiscard]] std::unique_ptr<JournalStorage> open_journal_file(const std::filesystem::path& path);

} // namespace trading::execution
