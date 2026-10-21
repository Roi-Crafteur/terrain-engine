#include "planet_parallel.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

namespace terrain::planet {

int resolveThreads(int requested) {
    if (requested > 0) {
        return requested;
    }
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

void parallelBlocks(int count, int blockSize, int threads,
                    const std::function<void(int begin, int end)>& body) {
    if (count <= 0) {
        return;
    }
    const int blocks = (count + blockSize - 1) / blockSize;
    const int workers = std::min(std::max(threads, 1), blocks);
    if (workers == 1) {
        for (int block = 0; block < blocks; ++block) {
            body(block * blockSize, std::min(count, (block + 1) * blockSize));
        }
        return;
    }
    std::vector<std::exception_ptr> errors(static_cast<std::size_t>(blocks));
    std::atomic<int> next{0};
    const auto work = [&] {
        for (int block = next.fetch_add(1); block < blocks; block = next.fetch_add(1)) {
            try {
                body(block * blockSize, std::min(count, (block + 1) * blockSize));
            } catch (...) {
                errors[static_cast<std::size_t>(block)] = std::current_exception();
            }
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers - 1));
    for (int t = 1; t < workers; ++t) {
        try {
            pool.emplace_back(work);
        } catch (...) {
            break; // no more threads: the ones started and this one do all the blocks
        }
    }
    work();
    for (std::thread& thread : pool) {
        thread.join();
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

} // namespace terrain::planet
