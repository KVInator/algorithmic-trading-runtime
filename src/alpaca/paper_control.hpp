#pragma once
#include "paper_transport.hpp"

namespace trading {
struct PaperControlConfiguration {
    std::string owner;
    std::int64_t health_interval_ms{30000};
    std::int64_t health_budget_ms{5000};
};

// Separate policy journal; accounting and reservations remain in the schema-3 owner.
// Serialized callers must service this owner regularly. No background timer is implied.
class PaperControl {
public:
    [[nodiscard]] static PaperControl create(std::unique_ptr<execution::JournalStorage> storage,
                                             PaperControlConfiguration configuration,
                                             execution::JournaledPaperRuntime& runtime, PaperSession& session,
                                             PaperSession::Clock clock);
    [[nodiscard]] static PaperControl recover(std::unique_ptr<execution::JournalStorage> storage,
                                              PaperControlConfiguration configuration,
                                              execution::JournaledPaperRuntime& runtime,
                                              PaperSession& session, PaperSession::Clock clock);
    PaperControl(PaperControl&&) noexcept = default;
    PaperControl(const PaperControl&) = delete;
    [[nodiscard]] PaperHttpResponse submit(const execution::OrderRequest& order, std::string expires_at);
    void service();
    void stop(std::string reason);
    [[nodiscard]] nlohmann::json snapshot() const;

private:
    PaperControl(std::unique_ptr<execution::JournalStorage>, PaperControlConfiguration,
                 execution::JournaledPaperRuntime&, PaperSession&, PaperSession::Clock);
    void commit(nlohmann::json command);
    void cancel_due(const PaperReceipt& now);
    [[nodiscard]] PaperReceipt observe_clock();
    std::unique_ptr<execution::JournalStorage> storage_;
    PaperControlConfiguration configuration_;
    execution::JournaledPaperRuntime& runtime_;
    PaperSession& session_;
    PaperSession::Clock clock_;
    nlohmann::json state_{{"stopped", false}, {"reason", ""}, {"deadlines", nlohmann::json::object()}};
    std::size_t sequence_{0}, bytes_{0};
    std::string previous_ = std::string(64, '0');
    bool healthy_{true};
    unsigned journal_schema_version_{2};
    std::string last_action_error_;
    std::optional<PaperReceipt> last_clock_;
    std::optional<std::int64_t> last_health_;
};
} // namespace trading
