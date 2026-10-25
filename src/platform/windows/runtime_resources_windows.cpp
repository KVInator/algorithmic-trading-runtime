#include "runtime_benchmark.hpp"
#include <windows.h>
#include <psapi.h>
#include <cstdlib>
#include <thread>

namespace trading::execution {
ProcessResources process_resources() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return {};
    return {static_cast<std::uint64_t>(counters.WorkingSetSize),
            static_cast<std::uint64_t>(counters.PeakWorkingSetSize)};
}
nlohmann::json benchmark_environment() {
    // This public OS descriptor contains processor information, not credentials.
    char *value = nullptr;
    std::size_t size = 0;
    const auto status = _dupenv_s(&value, &size, "PROCESSOR_IDENTIFIER");
    const std::string cpu = status == 0 && value ? value : "unavailable";
    std::free(value);
    return {{"os", "Windows x64"},
            {"cpu_model_os_reported", cpu},
            {"logical_processors", std::thread::hardware_concurrency()},
            {"resource_source", "GetProcessMemoryInfo; process-wide working set/peak"},
            {"clock", "std::chrono::steady_clock"}};
}
} // namespace trading::execution
