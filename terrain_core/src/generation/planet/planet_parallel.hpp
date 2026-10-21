#pragma once

#include <functional>

// Internal to the planet level: deterministic parallel loops. The work is cut into fixed blocks
// that the threads take in turn; each block writes only its own outputs from read-only inputs,
// so the results never depend on the number of threads or on the scheduling.
namespace terrain::planet {

// Number of threads to use: requested if above 0, else the hardware concurrency (at least 1).
[[nodiscard]] int resolveThreads(int requested);

// Calls body(begin, end) for the blocks [0, blockSize), [blockSize, 2 blockSize)... of
// [0, count), on up to threads threads (inline on the calling thread with 1 thread or 1 block).
// If blocks throw, the exception of the first one (lowest begin) is rethrown once all are done.
void parallelBlocks(int count, int blockSize, int threads,
                    const std::function<void(int begin, int end)>& body);

// Rows of a grid in blocks of kRowBlock rows.
inline constexpr int kRowBlock = 16;
inline void parallelRows(int rows, int threads,
                         const std::function<void(int begin, int end)>& body) {
    parallelBlocks(rows, kRowBlock, threads, body);
}

} // namespace terrain::planet
