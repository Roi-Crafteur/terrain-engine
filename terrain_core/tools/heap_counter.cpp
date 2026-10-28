// The global operator new and delete of the executables that measure their heap
// (heap_counter.hpp): every block asked for is counted, then freed with malloc's own free.

#include "heap_counter.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::size_t> gLive{0};
std::atomic<std::size_t> gPeak{0};

// Just before each block: where malloc put it and the bytes asked for.
struct Header {
    void* raw;
    std::size_t size;
};

void* allocate(std::size_t size, std::size_t alignment) noexcept {
    if (alignment < alignof(Header)) {
        alignment = alignof(Header);
    }
    void* raw = std::malloc(size + sizeof(Header) + alignment);
    if (raw == nullptr) {
        return nullptr;
    }
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment) - 1;
    const std::uintptr_t user = (first + mask) & ~mask;
    Header* header = reinterpret_cast<Header*>(user) - 1;
    header->raw = raw;
    header->size = size;
    const std::size_t now = gLive.fetch_add(size, std::memory_order_relaxed) + size;
    std::size_t peak = gPeak.load(std::memory_order_relaxed);
    while (now > peak && !gPeak.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
    }
    return reinterpret_cast<void*>(user);
}

void* allocateOrThrow(std::size_t size, std::size_t alignment) {
    void* block = allocate(size == 0 ? 1 : size, alignment);
    if (block == nullptr) {
        throw std::bad_alloc();
    }
    return block;
}

void release(void* block) noexcept {
    if (block == nullptr) {
        return;
    }
    Header* header = static_cast<Header*>(block) - 1;
    gLive.fetch_sub(header->size, std::memory_order_relaxed);
    std::free(header->raw);
}

constexpr std::size_t kDefault = alignof(std::max_align_t);

std::size_t value(std::align_val_t alignment) {
    return static_cast<std::size_t>(alignment);
}

} // namespace

namespace heap_counter {

std::size_t live() {
    return gLive.load(std::memory_order_relaxed);
}

std::size_t peak() {
    return gPeak.load(std::memory_order_relaxed);
}

void resetPeak() {
    gPeak.store(gLive.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

} // namespace heap_counter

void* operator new(std::size_t size) {
    return allocateOrThrow(size, kDefault);
}
void* operator new[](std::size_t size) {
    return allocateOrThrow(size, kDefault);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateOrThrow(size, value(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateOrThrow(size, value(alignment));
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size == 0 ? 1 : size, kDefault);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size == 0 ? 1 : size, kDefault);
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate(size == 0 ? 1 : size, value(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    return allocate(size == 0 ? 1 : size, value(alignment));
}

void operator delete(void* block) noexcept {
    release(block);
}
void operator delete[](void* block) noexcept {
    release(block);
}
void operator delete(void* block, std::size_t) noexcept {
    release(block);
}
void operator delete[](void* block, std::size_t) noexcept {
    release(block);
}
void operator delete(void* block, std::align_val_t) noexcept {
    release(block);
}
void operator delete[](void* block, std::align_val_t) noexcept {
    release(block);
}
void operator delete(void* block, std::size_t, std::align_val_t) noexcept {
    release(block);
}
void operator delete[](void* block, std::size_t, std::align_val_t) noexcept {
    release(block);
}
void operator delete(void* block, const std::nothrow_t&) noexcept {
    release(block);
}
void operator delete[](void* block, const std::nothrow_t&) noexcept {
    release(block);
}
void operator delete(void* block, std::align_val_t, const std::nothrow_t&) noexcept {
    release(block);
}
void operator delete[](void* block, std::align_val_t, const std::nothrow_t&) noexcept {
    release(block);
}
