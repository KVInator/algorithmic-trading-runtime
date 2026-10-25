#include "runtime_benchmark.hpp"
#include <fstream>
#include <sstream>
#include <thread>
#include <sys/utsname.h>

namespace trading::execution {
ProcessResources process_resources() {
    ProcessResources r;
    std::ifstream input{"/proc/self/status"};
    for (std::string line; std::getline(input, line);) {
        std::istringstream row{line};
        std::string key, units;
        std::uint64_t value{};
        if (row >> key >> value >> units && units == "kB") {
            if (key == "VmRSS:")
                r.resident_bytes = value * 1024;
            if (key == "VmHWM:")
                r.peak_resident_bytes = value * 1024;
        }
    }
    return r;
}
nlohmann::json benchmark_environment() {
    struct utsname info{};
    const std::string os = ::uname(&info) == 0
                               ? std::string{info.sysname} + " " + info.release + " " + info.machine
                               : "unavailable";
    std::string cpu = "unavailable";
    std::ifstream input{"/proc/cpuinfo"};
    for (std::string line; std::getline(input, line);)
        if (line.starts_with("model name")) {
            const auto at = line.find(':');
            if (at != line.npos)
                cpu = line.substr(at + 2);
            break;
        }
    return {{"os", os},
            {"cpu_model_os_reported", cpu},
            {"logical_processors", std::thread::hardware_concurrency()},
            {"resource_source", "/proc/self/status; process-wide RSS/HWM"},
            {"clock", "std::chrono::steady_clock"}};
}
} // namespace trading::execution
