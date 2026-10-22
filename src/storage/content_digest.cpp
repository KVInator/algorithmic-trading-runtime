#include "content_digest.hpp"
#include "artifact_io.hpp"

namespace trading {

std::string sha256_file(const std::filesystem::path& path) { return sha256(read_text_file(path)); }

} // namespace trading
