#pragma once

#include "journaled_runtime.hpp"

#include <filesystem>

namespace trading::execution {

struct RecoveryCheck { std::string name; bool passed; };
struct RecoveryCheckpoint {
    std::string scenario;
    std::string stage;
    AccountSnapshot account;
    OrderView order;
    std::size_t journal_records;
};
struct RecoveryDiagnosticResult {
    std::vector<RecoveryCheck> checks;
    std::vector<RecoveryCheckpoint> checkpoints;
    std::vector<std::string> journal_files;
    std::size_t preserved_tail_bytes;
};

[[nodiscard]] RecoveryDiagnosticResult run_recovery_diagnostic(const std::filesystem::path& directory);

} // namespace trading::execution
