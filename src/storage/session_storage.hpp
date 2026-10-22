#pragma once

#include "session_download.hpp"

#include <filesystem>
#include <optional>

namespace trading {

// Creates a new run directory. metadata.json is published last and marks a
// finished write; a missing marker means an interrupted or failed write.
[[nodiscard]] std::filesystem::path save_session_download(
    const SessionDownload& download, const std::filesystem::path& output_root);

[[nodiscard]] std::string session_page_filename(std::size_t index);

// Reconstructs the download from retained raw pages and checks the CSV and
// metadata against that reconstruction. No network requests are made.
[[nodiscard]] SessionDownload load_session_download(
    const std::filesystem::path& directory, const HistoricalBarRequest& expected);

// Reads the request from stored metadata, then applies the same raw-page,
// CSV and metadata verification as the explicit-request overload.
[[nodiscard]] SessionDownload load_session_download(const std::filesystem::path& directory);

[[nodiscard]] std::optional<std::filesystem::path> find_latest_session_run(
    const std::filesystem::path& root, const HistoricalBarRequest& request);

} // namespace trading
