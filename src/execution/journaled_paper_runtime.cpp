#include "paper_execution.hpp"
#include "journal_frame.hpp"
#include "runtime_json.hpp"
#include "content_digest.hpp"
#include <type_traits>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
static_assert(std::is_nothrow_move_assignable_v<PaperExecutionState>);
const std::string zero(64, '0');

Json header(const PaperExecutionConfiguration& c) {
    return {{"kind", "header"},
            {"schema_version", 3},
            {"model_contract", "paper_whole_share_nanodollar_v1"},
            {"sequence", 0},
            {"previous_sha256", zero},
            {"configuration",
             {{"account_id", c.account_id},
              {"initial_cash_usd", c.initial_cash_usd},
              {"maximum_position_shares", c.limits.maximum_position_shares},
              {"maximum_open_orders", c.limits.maximum_open_orders},
              {"fee_policy", c.fee_policy},
              {"money_unit", "USD_1e-9"},
              {"order_policy", "SPY_simple_DAY_regular_hours_v1"}}}};
}

Json entry(const Json& command, const PaperOutcome& result, const PaperExecutionState& state,
           std::size_t count, const std::string& previous) {
    return {{"kind", "paper_input"},
            {"sequence", count},
            {"previous_sha256", previous},
            {"command", command},
            {"outcome", name(result.outcome)},
            {"reason", result.reason},
            {"account", account_units_json(state.account())},
            {"state_sha256", sha256(state.snapshot().dump())}};
}

struct Loaded {
    PaperExecutionState state;
    std::size_t count;
    std::size_t complete;
    std::string digest;
    std::string raw;
};

Loaded load(JournalStorage& storage, const PaperExecutionConfiguration& c) {
    Loaded r{PaperExecutionState{c}, 0, 0, zero, storage.read_all()};
    if (r.raw.size() > maximum_journal_bytes)
        throw JournalError("paper journal exceeds 32 MiB");
    bool seen = false;
    while (true) {
        const auto end = r.raw.find('\n', r.complete);
        if (end == r.raw.npos)
            break;
        auto [value, digest] =
            JournalFrame::decode(std::string_view{r.raw}.substr(r.complete, end - r.complete));
        try {
            if (!seen) {
                if (value != header(c))
                    throw JournalError("paper journal header/model/configuration differs");
                seen = true;
            } else {
                if (r.count >= maximum_input_records)
                    throw JournalError("paper journal record limit exceeded");
                const auto command = value.at("command");
                const auto result = r.state.apply(command);
                if (value != entry(command, result, r.state, r.count + 1, r.digest))
                    throw JournalError("paper journal chain, sequence or replay checkpoint differs");
                ++r.count;
            }
        } catch (const Json::exception&) {
            throw JournalError("paper journal schema failure");
        }
        r.complete = end + 1;
        r.digest = std::move(digest);
    }
    if (!seen)
        throw JournalError("paper journal requires a complete schema-3 header");
    if (r.raw.size() - r.complete > maximum_frame_bytes)
        throw JournalError("paper journal tail exceeds 64 KiB");
    return r;
}

void require_storage(const std::unique_ptr<JournalStorage>& storage) {
    if (!storage)
        throw JournalError("paper journal storage is required");
}
} // namespace

JournaledPaperRuntime::JournaledPaperRuntime(std::unique_ptr<JournalStorage> storage,
                                             PaperExecutionState state, std::size_t count, std::size_t bytes,
                                             std::string digest)
    : storage_{std::move(storage)}, state_{std::move(state)}, count_{count}, bytes_{bytes},
      digest_{std::move(digest)} {}

JournaledPaperRuntime JournaledPaperRuntime::create(std::unique_ptr<JournalStorage> storage,
                                                    const PaperExecutionConfiguration& c) {
    require_storage(storage);
    PaperExecutionState state{c};
    if (!storage->read_all().empty())
        throw JournalError("new paper journal must be empty");
    auto [bytes, digest] = JournalFrame::encode(header(c));
    storage->append_and_sync(bytes);
    JournaledPaperRuntime result{std::move(storage), std::move(state), 0, bytes.size(), std::move(digest)};
    static_cast<void>(result.disconnect());
    return result;
}

JournaledPaperRuntime JournaledPaperRuntime::recover(std::unique_ptr<JournalStorage> storage,
                                                     const PaperExecutionConfiguration& c) {
    require_storage(storage);
    auto loaded = load(*storage, c);
    if (loaded.complete != loaded.raw.size())
        throw JournalError("paper journal has an incomplete tail; separate prefix recovery required");
    JournaledPaperRuntime result{std::move(storage), std::move(loaded.state), loaded.count, loaded.complete,
                                 std::move(loaded.digest)};
    static_cast<void>(result.disconnect());
    return result;
}

JournaledPaperRuntime JournaledPaperRuntime::recover_prefix(std::unique_ptr<JournalStorage> source,
                                                            std::unique_ptr<JournalStorage> destination,
                                                            const PaperExecutionConfiguration& c) {
    require_storage(source);
    require_storage(destination);
    auto loaded = load(*source, c);
    if (loaded.complete == loaded.raw.size() || !destination->read_all().empty())
        throw JournalError("prefix recovery requires a torn source and empty destination");
    destination->append_and_sync(std::string_view{loaded.raw}.substr(0, loaded.complete));
    JournaledPaperRuntime result{std::move(destination), std::move(loaded.state), loaded.count,
                                 loaded.complete, std::move(loaded.digest)};
    static_cast<void>(result.disconnect());
    return result;
}

bool JournaledPaperRuntime::healthy() const noexcept {
    return healthy_ && static_cast<bool>(storage_);
}

void JournaledPaperRuntime::ensure_healthy() const {
    if (!healthy())
        throw JournalError("paper writer is uncertain; close and recover before continuing");
}

PaperOutcome JournaledPaperRuntime::execute(const Json& command) {
    ensure_healthy();
    try {
        if (count_ >= maximum_input_records)
            throw JournalError("paper journal record limit exceeded");
        auto candidate = state_;
        auto outcome = candidate.apply(command);
        auto [bytes, digest] = JournalFrame::encode(entry(command, outcome, candidate, count_ + 1, digest_));
        if (bytes.size() > maximum_journal_bytes - bytes_)
            throw JournalError("paper journal exceeds 32 MiB");
        storage_->append_and_sync(bytes);
        state_ = std::move(candidate);
        digest_ = std::move(digest);
        bytes_ += bytes.size();
        ++count_;
        return outcome;
    } catch (...) {
        healthy_ = false;
        throw;
    }
}

PaperOutcome JournaledPaperRuntime::submit(const OrderRequest& order) {
    return execute(input_json(SubmitInput{order}));
}

PaperOutcome JournaledPaperRuntime::request_cancel(std::string client, std::string cancel) {
    return execute(
        Json{{"kind", "cancel"}, {"client_id", std::move(client)}, {"cancel_id", std::move(cancel)}});
}

PaperOutcome JournaledPaperRuntime::receive(std::string raw, std::string receipt) {
    return execute(Json{{"kind", "stream"}, {"raw", std::move(raw)}, {"received_at", std::move(receipt)}});
}

PaperOutcome JournaledPaperRuntime::observe_order(std::string raw) {
    return execute(Json{{"kind", "order"}, {"raw", std::move(raw)}});
}

PaperOutcome JournaledPaperRuntime::reconcile(std::string account, std::string positions,
                                              std::string orders) {
    return execute(Json{{"kind", "reconcile"},
                        {"account", std::move(account)},
                        {"positions", std::move(positions)},
                        {"orders", std::move(orders)}});
}

PaperOutcome JournaledPaperRuntime::disconnect() {
    return execute(Json{{"kind", "disconnect"}});
}

Json JournaledPaperRuntime::snapshot() const {
    ensure_healthy();
    auto snapshot = state_.snapshot();
    snapshot["durable_records"] = count_;
    snapshot["journal_sha256"] = digest_;
    return snapshot;
}
} // namespace trading::execution
