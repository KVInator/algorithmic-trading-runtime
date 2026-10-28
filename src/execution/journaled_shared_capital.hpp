#pragma once
#include "shared_capital.hpp"
#include "journaled_runtime.hpp"

namespace trading::execution {
struct SharedJournalIdentity {
    std::uint64_t generation{};
    std::string previous_segment_sha256 = std::string(64, '0');
    bool operator==(const SharedJournalIdentity&) const = default;
};
// Schema 4. A successor starts only after its flat predecessor is durably
// sealed. Every dispatch authorization follows synchronized journal append.
class JournaledSharedCapital {
public:
    [[nodiscard]] static JournaledSharedCapital create(std::unique_ptr<JournalStorage> storage, const SharedCapitalConfiguration& configuration);
    [[nodiscard]] static JournaledSharedCapital recover(std::unique_ptr<JournalStorage> storage, const SharedCapitalConfiguration& configuration,
        const SharedJournalIdentity& identity = {});
    [[nodiscard]] static JournaledSharedCapital recover_prefix(std::unique_ptr<JournalStorage> source, std::unique_ptr<JournalStorage> destination,
        const SharedCapitalConfiguration& configuration, const SharedJournalIdentity& identity = {});
    [[nodiscard]] JournaledSharedCapital handover(std::unique_ptr<JournalStorage> destination);
    [[nodiscard]] SharedResult execute(const SharedInput& input);
    [[nodiscard]] SharedCapitalSnapshot snapshot() const;
    [[nodiscard]] SharedCapitalConfiguration configuration() const;
    [[nodiscard]] SharedJournalIdentity identity() const;
    [[nodiscard]] JournalInspection inspection() const;
    [[nodiscard]] bool healthy() const noexcept;
    JournaledSharedCapital(JournaledSharedCapital&&) noexcept = default;
    JournaledSharedCapital& operator=(JournaledSharedCapital&&) noexcept = default;
    JournaledSharedCapital(const JournaledSharedCapital&) = delete;
    JournaledSharedCapital& operator=(const JournaledSharedCapital&) = delete;
private:
    JournaledSharedCapital(std::unique_ptr<JournalStorage> storage, SharedCapitalBook book, SharedJournalIdentity identity, JournalInspection inspection);
    [[nodiscard]] static JournaledSharedCapital start(std::unique_ptr<JournalStorage> storage, SharedCapitalBook book, SharedJournalIdentity identity);
    void ensure_healthy() const;
    std::unique_ptr<JournalStorage> storage_;
    SharedCapitalBook book_;
    SharedJournalIdentity identity_;
    JournalInspection inspection_;
    bool healthy_{true};
};
} // namespace trading::execution
