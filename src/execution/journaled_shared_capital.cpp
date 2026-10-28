#include "journaled_shared_capital.hpp"
#include "shared_capital_json.hpp"
#include "journal_frame.hpp"
#include <algorithm>
#include <type_traits>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
const std::string zero_digest(64, '0');
void identity_valid(const SharedJournalIdentity& id) {
    if (id.generation > 1000000000 || id.previous_segment_sha256.size() != 64 ||
        !std::all_of(id.previous_segment_sha256.begin(), id.previous_segment_sha256.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        ((id.generation == 0) != (id.previous_segment_sha256 == zero_digest))) { throw JournalError("invalid shared journal generation or parent identity"); }
}
void require_storage(const std::unique_ptr<JournalStorage>& p) { if (!p) { throw JournalError("shared journal requires owned storage"); } }
Json header(const SharedCapitalBook& book, const SharedJournalIdentity& id, const SharedFlatCheckpoint& checkpoint) {
    return {{"kind", "header"}, {"schema_version", 4}, {"model_contract", "shared_capital_v1"}, {"sequence", 0},
        {"previous_sha256", zero_digest}, {"generation", id.generation}, {"previous_segment_sha256", id.previous_segment_sha256},
        {"configuration", shared_configuration_json(book.configuration())}, {"flat_checkpoint", shared_checkpoint_json(checkpoint)}};
}
Json entry(const SharedInput& input, const SharedResult& result, const SharedCapitalBook& book, const JournalInspection& previous) {
    return {{"kind", "input"}, {"sequence", previous.input_records + 1}, {"previous_sha256", previous.last_sha256},
        {"input", shared_input_json(input)}, {"result", shared_result_json(result)}, {"checkpoint", shared_snapshot_json(book.snapshot())}};
}
struct Loaded { SharedCapitalBook book; JournalInspection inspection; std::string bytes; };
Loaded load(JournalStorage& storage, const SharedCapitalConfiguration& config, const SharedJournalIdentity& id) {
    identity_valid(id);
    Loaded loaded{SharedCapitalBook{config}, {0, 0, 0, "", 4}, storage.read_all()};
    if (loaded.bytes.size() > maximum_journal_bytes) { throw JournalError("shared segment exceeds 32 MiB"); }
    std::size_t offset = 0; bool first = true;
    while (true) {
        const auto end = loaded.bytes.find('\n', offset);
        if (end == std::string::npos) { break; }
        auto [value, digest] = JournalFrame::decode(std::string_view{loaded.bytes}.substr(offset, end - offset));
        try {
            if (first) {
                const auto checkpoint = parse_shared_checkpoint(value.at("flat_checkpoint"));
                auto root = SharedCapitalBook{config};
                if (id.generation == 0 && checkpoint != root.flat_checkpoint()) { throw JournalError("root shared checkpoint differs from initial capital"); }
                loaded.book = SharedCapitalBook{config, id.generation, checkpoint};
                if (value != header(loaded.book, id, checkpoint)) { throw JournalError("shared header, configuration or parent differs"); }
                first = false;
            } else {
                if (loaded.book.snapshot().sealed || loaded.inspection.input_records >= maximum_input_records) { throw JournalError("input after seal or shared record limit"); }
                const auto input = parse_shared_input(value.at("input"));
                const auto result = loaded.book.apply(input);
                if (value != entry(input, result, loaded.book, loaded.inspection)) { throw JournalError("shared sequence, hash chain or deterministic checkpoint differs"); }
                ++loaded.inspection.input_records;
            }
        } catch (const Json::exception& error) { throw JournalError(std::string{"shared journal JSON failure: "} + error.what()); }
        catch (const std::invalid_argument& error) { throw JournalError(std::string{"shared journal input failure: "} + error.what()); }
        loaded.inspection.last_sha256 = std::move(digest);
        offset = end + 1;
    }
    if (first) { throw JournalError("shared segment has no complete valid header"); }
    loaded.inspection.complete_bytes = offset;
    loaded.inspection.incomplete_tail_bytes = loaded.bytes.size() - offset;
    if (loaded.inspection.incomplete_tail_bytes > maximum_frame_bytes) { throw JournalError("shared incomplete tail exceeds one frame"); }
    return loaded;
}
}
static_assert(std::is_nothrow_move_assignable_v<SharedCapitalBook>);
JournaledSharedCapital::JournaledSharedCapital(std::unique_ptr<JournalStorage> storage, SharedCapitalBook book,
    SharedJournalIdentity id, JournalInspection inspection)
    : storage_(std::move(storage)), book_(std::move(book)), identity_(std::move(id)), inspection_(std::move(inspection)) {}
JournaledSharedCapital JournaledSharedCapital::start(std::unique_ptr<JournalStorage> storage, SharedCapitalBook book, SharedJournalIdentity id) {
    require_storage(storage); identity_valid(id);
    if (!storage->read_all().empty()) { throw JournalError("new shared segment must be empty"); }
    const auto checkpoint = book.flat_checkpoint();
    auto [bytes, digest] = JournalFrame::encode(header(book, id, checkpoint));
    storage->append_and_sync(bytes);
    JournaledSharedCapital result{std::move(storage), std::move(book), std::move(id), {0, bytes.size(), 0, std::move(digest), 4}};
    static_cast<void>(result.execute(SharedConnection{std::max<std::int64_t>(1, checkpoint.at_seconds), false}));
    return result;
}
JournaledSharedCapital JournaledSharedCapital::create(std::unique_ptr<JournalStorage> storage, const SharedCapitalConfiguration& config) {
    return start(std::move(storage), SharedCapitalBook{config}, {});
}
JournaledSharedCapital JournaledSharedCapital::recover(std::unique_ptr<JournalStorage> storage, const SharedCapitalConfiguration& config, const SharedJournalIdentity& id) {
    require_storage(storage);
    auto recovered = load(*storage, config, id);
    if (recovered.inspection.incomplete_tail_bytes) { throw JournalError("shared incomplete tail requires a separate verified-prefix destination"); }
    JournaledSharedCapital result{std::move(storage), std::move(recovered.book), id, std::move(recovered.inspection)};
    if (!result.book_.snapshot().sealed) { static_cast<void>(result.execute(SharedConnection{std::max<std::int64_t>(1, result.book_.snapshot().at_seconds), false})); }
    return result;
}
JournaledSharedCapital JournaledSharedCapital::recover_prefix(std::unique_ptr<JournalStorage> source, std::unique_ptr<JournalStorage> destination,
    const SharedCapitalConfiguration& config, const SharedJournalIdentity& id) {
    require_storage(source); require_storage(destination);
    auto recovered = load(*source, config, id);
    if (!recovered.inspection.incomplete_tail_bytes || !destination->read_all().empty()) { throw JournalError("shared prefix recovery requires an incomplete tail and empty destination"); }
    destination->append_and_sync(std::string_view{recovered.bytes}.substr(0, recovered.inspection.complete_bytes));
    JournaledSharedCapital result{std::move(destination), std::move(recovered.book), id, std::move(recovered.inspection)};
    result.inspection_.incomplete_tail_bytes = 0;
    if (!result.book_.snapshot().sealed) { static_cast<void>(result.execute(SharedConnection{std::max<std::int64_t>(1, result.book_.snapshot().at_seconds), false})); }
    return result;
}
bool JournaledSharedCapital::healthy() const noexcept { return healthy_ && static_cast<bool>(storage_); }
void JournaledSharedCapital::ensure_healthy() const { if (!healthy()) { throw JournalError("shared append is uncertain; recover this writer before continuing"); } }
SharedResult JournaledSharedCapital::execute(const SharedInput& input) {
    ensure_healthy();
    if (book_.snapshot().sealed) { throw JournalError("sealed shared segment cannot accept commands"); }
    if (inspection_.input_records >= maximum_input_records) { healthy_ = false; throw JournalError("shared record cap reached; routing closed"); }
    auto candidate = book_;
    auto result = candidate.apply(input);
    auto [bytes, digest] = JournalFrame::encode(entry(input, result, candidate, inspection_));
    if (bytes.size() > maximum_journal_bytes - inspection_.complete_bytes) { healthy_ = false; throw JournalError("shared byte cap reached; routing closed"); }
    try { storage_->append_and_sync(bytes); }
    catch (...) { healthy_ = false; throw; }
    book_ = std::move(candidate);
    inspection_.last_sha256 = std::move(digest); inspection_.complete_bytes += bytes.size(); ++inspection_.input_records;
    return result;
}
JournaledSharedCapital JournaledSharedCapital::handover(std::unique_ptr<JournalStorage> destination) {
    ensure_healthy(); require_storage(destination);
    if (!destination->read_all().empty()) { throw JournalError("shared successor must be empty"); }
    if (identity_.generation >= 1000000000) { throw JournalError("shared generation bound reached"); }
    const auto checkpoint = book_.flat_checkpoint();
    if (!book_.snapshot().sealed) {
        const auto result = execute(SharedSeal{std::max<std::int64_t>(1, checkpoint.at_seconds)});
        if (result.outcome != Outcome::applied) { throw JournalError("shared predecessor could not seal"); }
    }
    const SharedJournalIdentity next{identity_.generation + 1, inspection_.last_sha256};
    return start(std::move(destination), SharedCapitalBook{book_.configuration(), next.generation, book_.flat_checkpoint()}, next);
}
SharedCapitalSnapshot JournaledSharedCapital::snapshot() const { ensure_healthy(); return book_.snapshot(); }
SharedCapitalConfiguration JournaledSharedCapital::configuration() const { ensure_healthy(); return book_.configuration(); }
SharedJournalIdentity JournaledSharedCapital::identity() const { ensure_healthy(); return identity_; }
JournalInspection JournaledSharedCapital::inspection() const { ensure_healthy(); return inspection_; }
} // namespace trading::execution
