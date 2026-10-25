#include "journal_storage.hpp"

#include <Windows.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace trading::execution {
namespace {
[[noreturn]] void fail(const char* message) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), message);
}
class FileStorage final : public JournalStorage {
public:
    FileStorage(const std::filesystem::path& path, bool create) {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              create ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) { fail("cannot open journal or acquire writer ownership"); }
        try {
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle_, &info)) { fail("cannot inspect journal"); }
            if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
                GetFileType(handle_) != FILE_TYPE_DISK) { throw std::invalid_argument("journal must be a regular disk file"); }
        } catch (...) { CloseHandle(handle_); throw; }
    }
    ~FileStorage() override { CloseHandle(handle_); }
    std::string read_all() override {
        const auto length = size();
        std::string bytes(length, '\0');
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN)) { fail("cannot seek journal"); }
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto amount = static_cast<DWORD>(std::min(bytes.size() - offset,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD count = 0;
            if (!ReadFile(handle_, bytes.data() + offset, amount, &count, nullptr)) { fail("journal read failed"); }
            if (count == 0) { throw std::runtime_error("journal changed during read"); }
            offset += count;
        }
        return bytes;
    }
    void append_and_sync(std::string_view bytes) override {
        if (bytes.size() > maximum_journal_bytes || size() > maximum_journal_bytes - bytes.size()) {
            throw std::invalid_argument("journal append exceeds its byte limit");
        }
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle_, zero, nullptr, FILE_END)) { fail("cannot seek journal end"); }
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto amount = static_cast<DWORD>(std::min(bytes.size() - offset,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD count = 0;
            if (!WriteFile(handle_, bytes.data() + offset, amount, &count, nullptr)) { fail("journal append failed"); }
            if (count == 0) { throw std::runtime_error("journal append made no progress"); }
            offset += count;
        }
        if (!FlushFileBuffers(handle_)) { fail("journal synchronization failed"); }
    }
private:
    std::size_t size() const {
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(handle_, &length)) { fail("cannot measure journal"); }
        if (length.QuadPart < 0 || static_cast<unsigned long long>(length.QuadPart) > maximum_journal_bytes) {
            throw std::invalid_argument("journal exceeds its byte limit");
        }
        return static_cast<std::size_t>(length.QuadPart);
    }
    HANDLE handle_{INVALID_HANDLE_VALUE};
};
} // namespace

std::unique_ptr<JournalStorage> create_journal_file(const std::filesystem::path& path) { return std::make_unique<FileStorage>(path, true); }
std::unique_ptr<JournalStorage> open_journal_file(const std::filesystem::path& path) { return std::make_unique<FileStorage>(path, false); }

} // namespace trading::execution
