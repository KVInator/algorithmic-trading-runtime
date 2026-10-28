#include "child_channel.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>

namespace trading::execution {
namespace {
[[noreturn]] void fail(const char *text) {
    throw std::system_error(errno, std::generic_category(), text);
}
class PipeSignals {
  public:
    PipeSignals() {
        sigemptyset(&mask_);
        sigaddset(&mask_, SIGPIPE);
        const int status = pthread_sigmask(SIG_BLOCK, &mask_, &old_);
        if (status) {
            throw std::system_error(status, std::generic_category(), "cannot block pipe signal");
        }
        sigset_t pending{};
        sigpending(&pending);
        pending_ = sigismember(&pending, SIGPIPE) == 1;
    }
    ~PipeSignals() {
        if (!pending_) {
            timespec zero{};
            while (sigtimedwait(&mask_, nullptr, &zero) >= 0) {
            }
        }
        static_cast<void>(pthread_sigmask(SIG_SETMASK, &old_, nullptr));
    }

  private:
    sigset_t mask_{}, old_{};
    bool pending_{};
};
} // namespace
struct ChildChannel::Impl {
    pid_t pid{-1};
    int input{-1}, output{-1};
    ~Impl() {
        stop();
    }
    void stop() noexcept {
        if (input >= 0) {
            ::close(input);
            input = -1;
        }
        if (output >= 0) {
            ::close(output);
            output = -1;
        }
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
            }
            pid = -1;
        }
    }
    void transfer(int fd, char *data, std::size_t size, bool writing,
                  std::chrono::steady_clock::time_point deadline) {
        std::size_t offset = 0;
        while (offset < size) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0) {
                throw std::runtime_error("child exchange deadline");
            }
            pollfd p{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
            const auto ready = ::poll(&p, 1, static_cast<int>(left));
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fail("child pipe poll failed");
            }
            if (!ready) {
                throw std::runtime_error("child exchange deadline");
            }
            const auto count = writing ? ::write(fd, data + offset, size - offset)
                                       : ::read(fd, data + offset, size - offset);
            if (count < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                fail("child pipe transfer failed");
            }
            if (!count) {
                throw std::runtime_error("child pipe closed before complete frame");
            }
            offset += static_cast<std::size_t>(count);
        }
    }
};
ChildChannel::ChildChannel(const std::filesystem::path &exe, const std::vector<std::string> &args)
    : impl_(std::make_unique<Impl>()) {
    std::vector<std::string> values{exe.string()};
    values.insert(values.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (auto &v : values) {
        argv.push_back(v.data());
    }
    argv.push_back(nullptr);
    int input[2]{-1, -1}, output[2]{-1, -1};
    if (::pipe2(input, O_CLOEXEC) < 0) {
        fail("cannot create child input pipe");
    }
    if (::pipe2(output, O_CLOEXEC) < 0) {
        ::close(input[0]);
        ::close(input[1]);
        fail("cannot create child output pipe");
    }
    const auto pid = ::fork();
    if (pid < 0) {
        for (const auto fd : {input[0], input[1], output[0], output[1]}) {
            ::close(fd);
        }
        fail("cannot fork native peer");
    }
    if (pid == 0) {
        if (::dup2(input[0], STDIN_FILENO) < 0 || ::dup2(output[1], STDOUT_FILENO) < 0) {
            ::_exit(126);
        }
        for (const auto fd : {input[0], input[1], output[0], output[1]}) {
            if (fd > STDERR_FILENO) {
                ::close(fd);
            }
        }
        ::execv(exe.c_str(), argv.data());
        ::_exit(127);
    }
    impl_->pid = pid;
    impl_->input = input[1];
    impl_->output = output[0];
    ::close(input[0]);
    ::close(output[1]);
    for (const int fd : {impl_->input, impl_->output}) {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            fail("cannot set nonblocking child pipe");
        }
    }
}
ChildChannel::~ChildChannel() = default;
ChildChannel::ChildChannel(ChildChannel &&) noexcept = default;
ChildChannel &ChildChannel::operator=(ChildChannel &&) noexcept = default;
void ChildChannel::stop() noexcept {
    if (impl_) {
        impl_->stop();
    }
}
std::uint64_t ChildChannel::process_id() const noexcept {
    return impl_ && impl_->pid > 0 ? static_cast<std::uint64_t>(impl_->pid) : 0;
}
std::string ChildChannel::exchange(std::string payload, std::chrono::milliseconds budget) {
    if (!impl_ || impl_->pid <= 0 || payload.empty() || payload.size() > maximum_peer_payload_bytes ||
        budget.count() <= 0 || budget.count() > 30000) {
        throw std::invalid_argument("invalid child channel state, frame or deadline");
    }
    try {
        PipeSignals signals;
        const auto deadline = std::chrono::steady_clock::now() + budget;
        const auto n = static_cast<std::uint32_t>(payload.size());
        std::array<char, 4> prefix{};
        for (unsigned i = 0; i < 4; ++i) {
            prefix[i] = static_cast<char>((n >> (24 - 8 * i)) & 255U);
        }
        impl_->transfer(impl_->input, prefix.data(), prefix.size(), true, deadline);
        impl_->transfer(impl_->input, payload.data(), payload.size(), true, deadline);
        impl_->transfer(impl_->output, prefix.data(), prefix.size(), false, deadline);
        std::uint32_t length = 0;
        for (const auto c : prefix) {
            length = (length << 8) | static_cast<unsigned char>(c);
        }
        if (!length || length > maximum_peer_payload_bytes) {
            throw std::runtime_error("invalid child response length");
        }
        std::string response(length, '\0');
        impl_->transfer(impl_->output, response.data(), response.size(), false, deadline);
        return response;
    } catch (...) {
        stop();
        throw;
    }
}
} // namespace trading::execution
