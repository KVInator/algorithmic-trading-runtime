#include "journal_storage.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace trading::execution {
namespace {
[[noreturn]] void fail(const char* message) { throw std::system_error(errno, std::generic_category(), message); }
void sync_file(int fd) {
    while (::fsync(fd) != 0) { if (errno != EINTR) { fail("journal synchronization failed"); } }
}
class FileStorage final : public JournalStorage {
public:
    FileStorage(const std::filesystem::path& path, bool create) : path_{path}, sync_directory_{create} {
        fd_ = ::open(path.c_str(), O_RDWR | O_APPEND | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT | O_EXCL : 0), 0600);
        if (fd_ < 0) { fail("cannot open journal"); }
        try {
            if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) { fail("journal already has a writer"); }
            struct stat info{};
            if (::fstat(fd_, &info) != 0) { fail("cannot inspect journal"); }
            if (!S_ISREG(info.st_mode)) { throw std::invalid_argument("journal must be a regular file"); }
        } catch (...) { ::close(fd_); throw; }
    }
    ~FileStorage() override { ::close(fd_); }
    std::string read_all() override {
        struct stat info{};
        if (::fstat(fd_, &info) != 0) { fail("cannot measure journal"); }
        if (info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > maximum_journal_bytes) {
            throw std::invalid_argument("journal exceeds its byte limit");
        }
        std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = ::pread(fd_, bytes.data() + offset, bytes.size() - offset, static_cast<off_t>(offset));
            if (count < 0) { if (errno == EINTR) { continue; } fail("journal read failed"); }
            if (count == 0) { throw std::runtime_error("journal changed during read"); }
            offset += static_cast<std::size_t>(count);
        }
        return bytes;
    }
    void append_and_sync(std::string_view bytes) override {
        struct stat info{};
        if (::fstat(fd_, &info) != 0) { fail("cannot measure journal before append"); }
        if (info.st_size < 0 || bytes.size() > maximum_journal_bytes ||
            static_cast<std::uintmax_t>(info.st_size) > maximum_journal_bytes - bytes.size()) {
            throw std::invalid_argument("journal append exceeds its byte limit");
        }
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(fd_, bytes.data() + offset, bytes.size() - offset);
            if (count < 0) { if (errno == EINTR) { continue; } fail("journal append failed"); }
            if (count == 0) { throw std::runtime_error("journal append made no progress"); }
            offset += static_cast<std::size_t>(count);
        }
        sync_file(fd_);
        if (sync_directory_) {
            const auto parent = path_.parent_path().empty() ? std::filesystem::path{"."} : path_.parent_path();
            const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (directory < 0) { fail("cannot open journal parent for synchronization"); }
            try { sync_file(directory); } catch (...) { ::close(directory); throw; }
            ::close(directory);
            sync_directory_ = false;
        }
    }
private:
    std::filesystem::path path_;
    int fd_{-1};
    bool sync_directory_;
};
} // namespace

std::unique_ptr<JournalStorage> create_journal_file(const std::filesystem::path& path) { return std::make_unique<FileStorage>(path, true); }
std::unique_ptr<JournalStorage> open_journal_file(const std::filesystem::path& path) { return std::make_unique<FileStorage>(path, false); }

} // namespace trading::execution
