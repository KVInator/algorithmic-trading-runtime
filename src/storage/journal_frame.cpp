#include "journal_frame.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"

namespace trading::execution {
std::pair<std::string, std::string> JournalFrame::encode(const nlohmann::json& value) {
    auto payload = value.dump();
    auto digest = sha256(payload);
    payload += '\t';
    payload += digest;
    payload += '\n';
    if (payload.size() > maximum_frame_bytes)
        throw JournalError("journal frame exceeds 64 KiB");
    return {std::move(payload), std::move(digest)};
}

std::pair<nlohmann::json, std::string> JournalFrame::decode(std::string_view line) {
    if (line.size() >= maximum_frame_bytes)
        throw JournalError("journal frame exceeds 64 KiB");
    const auto separator = line.rfind('\t');
    if (separator == line.npos || line.size() - separator - 1 != 64)
        throw JournalError("complete journal frame has invalid checksum framing");
    const auto payload = line.substr(0, separator);
    std::string digest{line.substr(separator + 1)};
    if (sha256(payload) != digest)
        throw JournalError("complete journal frame checksum mismatch");
    try {
        return {parse_json_document(payload), std::move(digest)};
    } catch (const std::invalid_argument& e) {
        throw JournalError(std::string{"journal JSON failure: "} + e.what());
    }
}
} // namespace trading::execution
