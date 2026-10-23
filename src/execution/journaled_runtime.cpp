#include "journaled_runtime.hpp"

#include "content_digest.hpp"
#include "json_input.hpp"
#include "runtime_json.hpp"

#include <nlohmann/json.hpp>

#include <type_traits>
#include <utility>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maximum_frame_bytes = 64U * 1024U;
constexpr std::size_t maximum_input_records = 10000;
const std::string zero_digest(64, '0');
static_assert(std::is_nothrow_move_assignable_v<OrderRuntime>);

Json configuration_json(const RuntimeConfiguration& config) {
    return {{"symbol", config.symbol}, {"initial_cash_cents", config.initial_cash_cents},
            {"maximum_position_shares", config.limits.maximum_position_shares},
            {"maximum_open_orders", config.limits.maximum_open_orders}};
}
void validate_configuration(const RuntimeConfiguration& config) {
    static_cast<void>(OrderRuntime{config.symbol, config.initial_cash_cents, config.limits});
    if (config.limits.maximum_open_orders > maximum_input_records) {
        throw JournalError("journal configuration exceeds the supported order limit");
    }
}
Json header_json(const RuntimeConfiguration& config, int schema) {
    if (schema != 1 && schema != 2) { throw JournalError("unsupported journal schema"); }
    return {{"kind", "header"}, {"schema_version", schema}, {"model_contract", "whole_share_limit_v1"},
            {"sequence", 0}, {"previous_sha256", zero_digest}, {"configuration", configuration_json(config)}};
}
Json entry_json(const RuntimeInput& input, const TraceEntry& trace, const std::string& previous) {
    return {{"kind", "input"}, {"sequence", trace.sequence}, {"previous_sha256", previous},
            {"input", input_json(input)}, {"trace", trace_json(trace)}};
}
std::pair<std::string, std::string> frame(const Json& value) {
    auto payload = value.dump();
    auto digest = sha256(payload);
    payload += '\t'; payload += digest; payload += '\n';
    if (payload.size() > maximum_frame_bytes) { throw JournalError("journal frame exceeds 64 KiB"); }
    return {std::move(payload), std::move(digest)};
}
struct Loaded {
    OrderRuntime runtime;
    JournalInspection inspection;
    std::string bytes;
};
Loaded load(JournalStorage& storage, const RuntimeConfiguration& expected) {
    validate_configuration(expected);
    Loaded loaded{OrderRuntime{expected.symbol, expected.initial_cash_cents, expected.limits}, {0, 0, 0, ""}, storage.read_all()};
    if (loaded.bytes.size() > maximum_journal_bytes) { throw JournalError("journal exceeds 32 MiB"); }
    bool header_seen = false;
    std::size_t offset = 0;
    while (true) {
        const auto end = loaded.bytes.find('\n', offset);
        if (end == std::string::npos) { break; }
        if (end - offset + 1 > maximum_frame_bytes) { throw JournalError("journal frame exceeds 64 KiB"); }
        const std::string_view line{loaded.bytes.data() + offset, end - offset};
        const auto separator = line.rfind('\t');
        if (separator == std::string_view::npos || line.size() - separator - 1 != 64) {
            throw JournalError("complete journal frame has invalid checksum framing");
        }
        const auto payload = line.substr(0, separator);
        const std::string digest{line.substr(separator + 1)};
        if (sha256(payload) != digest) { throw JournalError("complete journal frame checksum mismatch"); }
        try {
            const auto value = parse_json_document(payload);
            if (!header_seen) {
                const auto& version = value.at("schema_version");
                const int schema = version == 1 ? 1 : version == 2 ? 2 : 0;
                if (value.dump() != header_json(expected, schema).dump()) {
                    throw JournalError("journal header, model version or expected configuration differs");
                }
                header_seen = true;
                loaded.inspection.schema_version = schema;
            } else {
                if (loaded.inspection.input_records >= maximum_input_records) { throw JournalError("journal record limit exceeded"); }
                const auto input = parse_runtime_input(value.at("input"));
                if (loaded.inspection.schema_version == 1 && std::holds_alternative<TargetIntent>(input)) {
                    throw JournalError("strategy targets require journal schema 2");
                }
                static_cast<void>(apply_input(loaded.runtime, input));
                if (value.dump() != entry_json(input, loaded.runtime.trace().back(), loaded.inspection.last_sha256).dump()) {
                    throw JournalError("journal sequence, chain or deterministic replay checkpoint differs");
                }
                ++loaded.inspection.input_records;
            }
        } catch (const Json::exception& error) {
            throw JournalError(std::string{"journal JSON schema failure: "} + error.what());
        } catch (const std::invalid_argument& error) {
            throw JournalError(std::string{"journal input failure: "} + error.what());
        }
        loaded.inspection.last_sha256 = digest;
        offset = end + 1;
    }
    if (!header_seen) { throw JournalError("journal has no complete valid header"); }
    loaded.inspection.complete_bytes = offset;
    loaded.inspection.incomplete_tail_bytes = loaded.bytes.size() - offset;
    if (loaded.inspection.incomplete_tail_bytes > maximum_frame_bytes) { throw JournalError("incomplete journal tail exceeds 64 KiB"); }
    return loaded;
}
void require_storage(const std::unique_ptr<JournalStorage>& storage) {
    if (!storage) { throw std::invalid_argument("journal storage is required"); }
}
} // namespace

JournaledRuntime::JournaledRuntime(std::unique_ptr<JournalStorage> storage, OrderRuntime runtime, JournalInspection inspection)
    : storage_{std::move(storage)}, runtime_{std::move(runtime)}, inspection_{std::move(inspection)} {}

JournaledRuntime JournaledRuntime::create(std::unique_ptr<JournalStorage> storage, const RuntimeConfiguration& config, int schema) {
    require_storage(storage);
    validate_configuration(config);
    if (!storage->read_all().empty()) { throw JournalError("new journal storage must be empty"); }
    auto [bytes, digest] = frame(header_json(config, schema));
    storage->append_and_sync(bytes);
    JournaledRuntime result{std::move(storage), OrderRuntime{config.symbol, config.initial_cash_cents, config.limits},
                            {0, bytes.size(), 0, std::move(digest), schema}};
    static_cast<void>(result.execute(ConnectionInput{false}));
    return result;
}
JournaledRuntime JournaledRuntime::recover(std::unique_ptr<JournalStorage> storage, const RuntimeConfiguration& expected) {
    require_storage(storage);
    auto loaded = load(*storage, expected);
    if (loaded.inspection.incomplete_tail_bytes != 0) { throw JournalError("incomplete journal tail requires a separate verified-prefix recovery file"); }
    JournaledRuntime result{std::move(storage), std::move(loaded.runtime), std::move(loaded.inspection)};
    static_cast<void>(result.execute(ConnectionInput{false}));
    return result;
}
JournaledRuntime JournaledRuntime::recover_prefix(std::unique_ptr<JournalStorage> source,
    std::unique_ptr<JournalStorage> destination, const RuntimeConfiguration& expected) {
    require_storage(source); require_storage(destination);
    auto loaded = load(*source, expected);
    if (loaded.inspection.incomplete_tail_bytes == 0) { throw JournalError("verified-prefix recovery requires an incomplete final frame"); }
    if (!destination->read_all().empty()) { throw JournalError("verified-prefix destination must be empty"); }
    destination->append_and_sync(std::string_view{loaded.bytes}.substr(0, loaded.inspection.complete_bytes));
    loaded.inspection.incomplete_tail_bytes = 0;
    JournaledRuntime result{std::move(destination), std::move(loaded.runtime), std::move(loaded.inspection)};
    static_cast<void>(result.execute(ConnectionInput{false}));
    return result;
}
JournalInspection JournaledRuntime::inspect(JournalStorage& storage, const RuntimeConfiguration& expected) {
    return load(storage, expected).inspection;
}
bool JournaledRuntime::healthy() const noexcept { return healthy_ && static_cast<bool>(storage_); }
void JournaledRuntime::ensure_healthy() const {
    if (!healthy()) { throw JournalError("journal outcome is uncertain; close this writer and recover before continuing"); }
}
Outcome JournaledRuntime::execute(const RuntimeInput& input) {
    ensure_healthy();
    if (inspection_.schema_version == 1 && std::holds_alternative<TargetIntent>(input)) {
        throw JournalError("strategy targets require a new schema-2 journal");
    }
    if (inspection_.input_records >= maximum_input_records) { throw JournalError("journal record limit exceeded"); }
    auto candidate = runtime_;
    const auto outcome = apply_input(candidate, input);
    auto [bytes, digest] = frame(entry_json(input, candidate.trace().back(), inspection_.last_sha256));
    if (bytes.size() > maximum_journal_bytes - inspection_.complete_bytes) { throw JournalError("journal exceeds 32 MiB"); }
    try { storage_->append_and_sync(bytes); }
    catch (...) { healthy_ = false; throw; }
    // All allocations occur before the durable append. The state publication is
    // a move; a crash after flush still replays the committed input on restart.
    runtime_ = std::move(candidate);
    inspection_.last_sha256 = std::move(digest);
    inspection_.complete_bytes += bytes.size();
    ++inspection_.input_records;
    return outcome;
}
AccountSnapshot JournaledRuntime::account() const { ensure_healthy(); return runtime_.account(); }
std::string JournaledRuntime::symbol() const { ensure_healthy(); return std::string{runtime_.symbol()}; }
OrderView JournaledRuntime::order(const std::string& id) const { ensure_healthy(); return runtime_.order(id); }
std::vector<OrderView> JournaledRuntime::orders() const { ensure_healthy(); return runtime_.orders(); }
std::optional<TargetIntent> JournaledRuntime::latest_target() const { ensure_healthy(); return runtime_.latest_target(); }
const std::vector<TraceEntry>& JournaledRuntime::trace() const { ensure_healthy(); return runtime_.trace(); }
JournalInspection JournaledRuntime::inspection() const { ensure_healthy(); return inspection_; }

} // namespace trading::execution
