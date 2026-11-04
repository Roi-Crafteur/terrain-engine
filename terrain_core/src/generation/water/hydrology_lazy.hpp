#pragma once

// The caches of the hydrology (agent 39), internal to terrain_core: objects made once on demand,
// by index, from any thread (the paths, floors, start levels and courses of the reaches, the fine
// lakes: hydrology_impl.hpp).
//
// - The first thread to need an object marks it as being made and makes it outside of any lock:
//   two threads make two objects at once, even in the same shard.
// - The other threads that need the same object wait for it, by steps of kLazyWait, and between
//   two steps call workStopPoint(): a stopped thread leaves (WorkStopped) even while another one
//   makes what it waits for (work_stop.hpp).
// - An object is published whole (release), or not at all: if its making throws (WorkStopped, or
//   anything else), its slot is empty again, the waiting threads wake and one of them makes it.
// - make() must never ask for its own index (the threads would wait for themselves); it may ask
//   for any other one, of this cache or another, as long as nothing asks back (the reaches ask
//   for their downstream courses and upstream levels only: no cycle).

#include "terrain/work_stop.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace terrain::water_detail {

// The shards of the locks of the caches, and the step of a wait.
inline constexpr std::size_t kShards = 64;
inline constexpr std::chrono::milliseconds kLazyWait{2};

template <class T>
class Lazy {
public:
    void resize(std::size_t n) {
        slots_ = std::make_unique<std::atomic<const T*>[]>(n);
        making_ = std::make_unique<bool[]>(n); // false: guarded by the lock of the shard
        for (std::size_t i = 0; i < n; ++i) {
            slots_[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    [[nodiscard]] bool has(std::size_t i) const {
        return slots_[i].load(std::memory_order_acquire) != nullptr;
    }
    template <class Make>
    const T& get(std::size_t i, Make&& make) {
        if (const T* p = slots_[i].load(std::memory_order_acquire)) {
            return *p;
        }
        Shard& shard = shards_[i % kShards];
        {
            std::unique_lock<std::mutex> lock(shard.mutex);
            for (;;) {
                if (const T* p = slots_[i].load(std::memory_order_acquire)) {
                    return *p;
                }
                if (!making_[i]) {
                    break;
                }
                shard.done.wait_for(lock, kLazyWait);
                workStopPoint(); // the lock is released as WorkStopped leaves
            }
            making_[i] = true;
        }
        const T* made = nullptr;
        try {
            auto object = std::make_unique<const T>(make());
            made = object.get();
            const std::lock_guard<std::mutex> own(ownedLock_);
            owned_.push_back(std::move(object));
        } catch (...) {
            finish(shard, i, nullptr);
            throw; // never swallowed: the caller stops (WorkStopped) or fails
        }
        finish(shard, i, made);
        return *made;
    }
    [[nodiscard]] std::size_t count() const {
        const std::lock_guard<std::mutex> own(ownedLock_);
        return owned_.size();
    }
    template <class Size>
    [[nodiscard]] std::size_t bytes(Size&& size) const {
        const std::lock_guard<std::mutex> own(ownedLock_);
        std::size_t total = 0;
        for (const auto& p : owned_) {
            total += size(*p);
        }
        return total;
    }

private:
    struct Shard {
        std::mutex mutex;
        std::condition_variable done;
    };

    // The end of the making of object i: published (made), or its slot empty again (null).
    void finish(Shard& shard, std::size_t i, const T* made) {
        {
            const std::lock_guard<std::mutex> lock(shard.mutex);
            if (made != nullptr) {
                slots_[i].store(made, std::memory_order_release);
            }
            making_[i] = false;
        }
        shard.done.notify_all();
    }

    std::unique_ptr<std::atomic<const T*>[]> slots_;
    std::unique_ptr<bool[]> making_;
    std::array<Shard, kShards> shards_;
    mutable std::mutex ownedLock_;
    std::vector<std::unique_ptr<const T>> owned_;
};

} // namespace terrain::water_detail
