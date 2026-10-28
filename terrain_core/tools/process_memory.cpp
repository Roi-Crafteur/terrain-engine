// The memory of the process as the system counts it (process_memory.hpp).

#include "process_memory.hpp"

#include <chrono>
#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace process_memory {

std::size_t resident() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.WorkingSetSize;
    }
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &count) != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<std::size_t>(info.resident_size);
#else
    std::FILE* file = std::fopen("/proc/self/statm", "r");
    if (file == nullptr) {
        return 0;
    }
    unsigned long size = 0;
    unsigned long pages = 0;
    const int read = std::fscanf(file, "%lu %lu", &size, &pages);
    std::fclose(file);
    const long page = sysconf(_SC_PAGESIZE);
    return read == 2 && page > 0 ? static_cast<std::size_t>(pages) * static_cast<std::size_t>(page)
                                 : 0;
#endif
}

std::size_t peakResident() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.PeakWorkingSetSize;
    }
    return 0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<std::size_t>(usage.ru_maxrss); // bytes
#else
    return static_cast<std::size_t>(usage.ru_maxrss) * 1024; // kilobytes
#endif
#endif
}

void returnFreedBlocks() {
#if defined(__GLIBC__)
    // A fixed threshold stops the dynamic one: every block from 1 MiB is mapped on its own.
    mallopt(M_MMAP_THRESHOLD, 1 << 20);
#endif
}

Sampler::Sampler() : thread_([this] { sample(); }) {}

Sampler::~Sampler() {
    stop_ = true;
    thread_.join();
}

std::size_t Sampler::takePeak() {
    const std::size_t now = resident();
    std::size_t peak = peak_.exchange(now);
    return peak > now ? peak : now;
}

void Sampler::sample() {
    while (!stop_) {
        const std::size_t now = resident();
        std::size_t peak = peak_.load();
        while (now > peak && !peak_.compare_exchange_weak(peak, now)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace process_memory
