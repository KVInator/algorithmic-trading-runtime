#include "child_channel.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace trading::execution {
namespace {
[[noreturn]] void fail(const char *message) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), message);
}
std::wstring argument(const std::wstring &value) {
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
} // namespace
struct ChildChannel::Impl {
    HANDLE process{nullptr}, input{nullptr}, output{nullptr};
    DWORD pid{};
    ~Impl() {
        stop();
    }
    void stop() noexcept {
        if (process) {
            TerminateProcess(process, 125);
            WaitForSingleObject(process, 1000);
            CloseHandle(process);
            process = nullptr;
            pid = 0;
        }
        for (auto *handle : {&input, &output}) {
            if (*handle) {
                CloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }
};
ChildChannel::ChildChannel(const std::filesystem::path &exe, const std::vector<std::string> &args)
    : impl_(std::make_unique<Impl>()) {
    SECURITY_ATTRIBUTES security{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE};
    HANDLE child_in{}, child_out{};
    if (!CreatePipe(&child_in, &impl_->input, &security, 65540)) {
        fail("cannot create child input pipe");
    }
    if (!CreatePipe(&impl_->output, &child_out, &security, 65540)) {
        CloseHandle(child_in);
        fail("cannot create child output pipe");
    }
    try {
        if (!SetHandleInformation(impl_->input, HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(impl_->output, HANDLE_FLAG_INHERIT, 0)) {
            fail("cannot isolate parent pipe handles");
        }
        STARTUPINFOW start{};
        start.cb = static_cast<DWORD>(sizeof(start));
        start.dwFlags = STARTF_USESTDHANDLES;
        start.hStdInput = child_in;
        start.hStdOutput = child_out;
        start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        std::wstring command = argument(exe.wstring());
        for (const auto &arg : args) {
            command += L' ';
            command += argument(std::filesystem::path{arg}.wstring());
        }
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                            nullptr, &start, &info)) {
            fail("cannot create native broker process");
        }
        impl_->process = info.hProcess;
        impl_->pid = info.dwProcessId;
        CloseHandle(info.hThread);
    } catch (...) {
        CloseHandle(child_in);
        CloseHandle(child_out);
        throw;
    }
    CloseHandle(child_in);
    CloseHandle(child_out);
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
    return impl_ ? impl_->pid : 0;
}
std::string ChildChannel::exchange(std::string payload, std::chrono::milliseconds budget) {
    if (!impl_ || !impl_->process || payload.empty() || payload.size() > maximum_peer_payload_bytes ||
        budget.count() <= 0 || budget.count() > 30000) {
        throw std::invalid_argument("invalid child channel state, frame or deadline");
    }
    try {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        const auto n = static_cast<std::uint32_t>(payload.size());
        std::string packet(4, '\0');
        for (unsigned i = 0; i < 4; ++i) {
            packet[i] = static_cast<char>((n >> (24 - 8 * i)) & 255U);
        }
        packet += payload;
        DWORD error = ERROR_SUCCESS;
        std::thread writer([&] {
            std::size_t offset = 0;
            while (offset < packet.size()) {
                DWORD written{};
                if (!WriteFile(impl_->input, packet.data() + offset,
                               static_cast<DWORD>(packet.size() - offset), &written, nullptr)) {
                    error = GetLastError();
                    return;
                }
                if (!written) {
                    error = ERROR_BROKEN_PIPE;
                    return;
                }
                offset += written;
            }
        });
        const auto wait = WaitForSingleObject(writer.native_handle(), static_cast<DWORD>(budget.count()));
        if (wait != WAIT_OBJECT_0) {
            CancelSynchronousIo(writer.native_handle());
            TerminateProcess(impl_->process, 125);
            writer.join();
            throw std::runtime_error("child write deadline");
        }
        writer.join();
        if (error != ERROR_SUCCESS) {
            throw std::system_error(static_cast<int>(error), std::system_category(), "child write failed");
        }
        const auto read = [&](char *data, std::size_t size) {
            std::size_t offset = 0;
            while (offset < size) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw std::runtime_error("child exchange deadline");
                }
                DWORD available{};
                if (!PeekNamedPipe(impl_->output, nullptr, 0, nullptr, &available, nullptr)) {
                    fail("child output pipe closed");
                }
                if (!available) {
                    if (WaitForSingleObject(impl_->process, 0) != WAIT_TIMEOUT) {
                        throw std::runtime_error("child exited before response");
                    }
                    Sleep(1);
                    continue;
                }
                DWORD received{};
                const auto amount =
                    static_cast<DWORD>(std::min(size - offset, static_cast<std::size_t>(available)));
                if (!ReadFile(impl_->output, data + offset, amount, &received, nullptr)) {
                    fail("child read failed");
                }
                if (!received) {
                    throw std::runtime_error("incomplete child response");
                }
                offset += received;
            }
        };
        std::array<char, 4> prefix{};
        read(prefix.data(), prefix.size());
        std::uint32_t length = 0;
        for (const auto c : prefix) {
            length = (length << 8) | static_cast<unsigned char>(c);
        }
        if (!length || length > maximum_peer_payload_bytes) {
            throw std::runtime_error("invalid child response length");
        }
        std::string response(length, '\0');
        read(response.data(), response.size());
        return response;
    } catch (...) {
        stop();
        throw;
    }
}
} // namespace trading::execution
