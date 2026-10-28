#pragma once

// The memory of the process as the system counts it, for the tools that measure their memory:
// the resident set on Linux and macOS, the working set on Windows. The platform code of these
// tools; terrain_core itself has none.

#include <atomic>
#include <cstddef>
#include <thread>

namespace process_memory {

// Bytes resident now, 0 if unknown.
[[nodiscard]] std::size_t resident();
// The most bytes resident since the process started, 0 if unknown.
[[nodiscard]] std::size_t peakResident();

// Makes the allocator give each large block back to the system when it is freed, as Windows does:
// glibc raises its mmap threshold up to 32 MiB after a large block is freed, then keeps the freed
// blocks below it in the process (nothing on the other systems).
void returnFreedBlocks();

// Samples resident() every 2 ms on its own thread, from its construction to its destruction,
// and keeps the most since the last reset: the peak of a stage, which the system does not keep
// (a short peak between two samples can be missed; peakResident() never misses it).
class Sampler {
public:
    Sampler();
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // The most bytes resident since the last reset, then a new peak from now.
    std::size_t takePeak();

private:
    void sample();

    std::atomic<bool> stop_{false};
    std::atomic<std::size_t> peak_{0};
    std::thread thread_;
};

} // namespace process_memory
