#pragma once

// The caches of the hydrology (agent 39), internal to terrain_core: objects made once on demand,
// by index, from any thread (the paths, floors, start levels and courses of the reaches, the fine
// lakes: hydrology_impl.hpp).
//
// - The first thread to need an object marks it as being made and makes it outside of any lock:
//   two threads make two objects at once, even in the same shard.
// - The other threads that need the same object wait for it, by steps of kLazyWait, and before
//   each step call workStopPoint(): a stopped thread leaves (WorkStopped) even while another one
//   makes what it waits for (work_stop.hpp). A flag raised by raiseWorkStop wakes them at once:
//   the cache wakes every shard (its WorkStopWaking), under the lock of each, so that no thread
//   misses it between its check and its wait. A cache made with the step kLazyForever (a test)
//   waits until woken: only that waking, or the object, lets a waiting thread out.
// - An object is published whole, or not at all: if its making throws (WorkStopped, or anything
//   else), its slot is empty again, the waiting threads wake and one of them makes it.
// - make() must never ask for its own index (the threads would wait for themselves); it may ask
//   for any other one, of this cache or another, as long as nothing asks back (the reaches ask
//   for their downstream courses and upstream levels only: no cycle).
// - A cache with a budget keeps the bytes of the objects it holds under it: past it, it drops
//   the least recently read ones down to three quarters of it, never the one just made (an
//   object wider than the budget is kept until the next one). A dropped object lives on while a
//   reader holds it (get gives a Ref, counted: hold it while reading, never a raw pointer past
//   it), and is made again, the same bits, when asked again. The objects are pure functions of
//   their index: nothing read depends on what was dropped.
// - Per index, a pointer and a flag (9 bytes: the reaches of a world are counted in millions);
//   the count of the readers, the tick of the last read and the size live in the object.

#include "terrain/work_stop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace terrain::water_detail {

// The shards of the locks of the caches, and the step of a wait (kLazyForever: until woken).
inline constexpr std::size_t kShards = 64;
inline constexpr std::chrono::milliseconds kLazyWait{2};
inline constexpr std::chrono::milliseconds kLazyForever = std::chrono::milliseconds::max();
// The budget of a cache that never drops anything (its slots bound it: one object per index).
inline constexpr std::size_t kUnbounded = std::numeric_limits<std::size_t>::max();

template <class T>
class Lazy {
    struct Node {
        explicit Node(T&& made) : value(std::move(made)) {}
        T value;
        std::atomic<std::uint32_t> refs{1};   // the cache's, while it holds it, and the Refs'
        std::atomic<std::uint64_t> used{0};   // the tick of the last read
        std::size_t size = 0;
    };
    static void release(Node* node) noexcept {
        if (node != nullptr && node->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete node;
        }
    }

public:
    using Size = std::size_t (*)(const T&);

    // An object of the cache, held while the Ref lives (the cache may drop it meanwhile).
    class Ref {
    public:
        Ref() noexcept = default;
        Ref(std::nullptr_t) noexcept {} // NOLINT: a Ref of nothing, as a null pointer
        Ref(const Ref& other) noexcept : node_(other.node_) {
            if (node_ != nullptr) {
                node_->refs.fetch_add(1, std::memory_order_relaxed);
            }
        }
        Ref(Ref&& other) noexcept : node_(std::exchange(other.node_, nullptr)) {}
        Ref& operator=(Ref other) noexcept {
            std::swap(node_, other.node_);
            return *this;
        }
        ~Ref() { release(node_); }
        [[nodiscard]] const T& operator*() const { return node_->value; }
        [[nodiscard]] const T* operator->() const { return &node_->value; }
        [[nodiscard]] const T* get() const { return node_ != nullptr ? &node_->value : nullptr; }
        [[nodiscard]] bool operator==(std::nullptr_t) const { return node_ == nullptr; }

    private:
        friend class Lazy;
        explicit Ref(Node* node) noexcept : node_(node) {} // takes one count
        Node* node_ = nullptr;
    };

    Lazy() = default;
    // A cache whose waiting threads wait by steps of step, or until woken (kLazyForever).
    explicit Lazy(std::chrono::milliseconds step) : step_(step) {}
    Lazy(const Lazy&) = delete;
    Lazy& operator=(const Lazy&) = delete;
    ~Lazy() {
        for (std::size_t i = 0; i < count_; ++i) {
            release(slots_[i]);
        }
    }

    // n slots, the objects held kept under budget bytes, each counted by size (both for a cache
    // with a budget only).
    void resize(std::size_t n, std::size_t budget = kUnbounded, Size size = nullptr) {
        slots_ = std::make_unique<Node*[]>(n); // null: guarded by the lock of the shard
        making_ = std::make_unique<bool[]>(n); // false: the same
        count_ = n;
        budget_ = size != nullptr ? budget : kUnbounded;
        size_ = size;
    }
    [[nodiscard]] bool has(std::size_t i) const {
        const std::lock_guard<std::mutex> lock(shards_[i % kShards].mutex);
        return slots_[i] != nullptr;
    }
    template <class Make>
    Ref get(std::size_t i, Make&& make) {
        Shard& shard = shards_[i % kShards];
        {
            std::unique_lock<std::mutex> lock(shard.mutex);
            for (;;) {
                if (Node* node = slots_[i]) {
                    node->refs.fetch_add(1, std::memory_order_relaxed);
                    if (budget_ != kUnbounded) {
                        node->used.store(clock_.load(std::memory_order_relaxed),
                                         std::memory_order_relaxed);
                    }
                    return Ref(node);
                }
                if (!making_[i]) {
                    break;
                }
                workStopPoint(); // the lock is released as WorkStopped leaves
                if (step_ == kLazyForever) {
                    shard.done.wait(lock);
                } else {
                    shard.done.wait_for(lock, step_);
                }
            }
            making_[i] = true;
        }
        Node* fresh = nullptr;
        try {
            fresh = new Node(make());
        } catch (...) {
            finish(shard, i, nullptr);
            throw; // never swallowed: the caller stops (WorkStopped) or fails
        }
        fresh->refs.store(2, std::memory_order_relaxed); // the cache's and the caller's
        if (budget_ != kUnbounded) {
            fresh->size = size_(fresh->value);
        }
        finish(shard, i, fresh);
        made_.fetch_add(1, std::memory_order_relaxed);
        if (budget_ != kUnbounded) {
            hold(i, fresh);
        }
        return Ref(fresh);
    }
    // The objects made so far (those made again after a drop counted again), and the bytes the
    // cache holds now.
    [[nodiscard]] std::size_t made() const { return made_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t bytes() const {
        const std::lock_guard<std::mutex> lock(budgetLock_);
        return bytes_;
    }

private:
    struct Shard {
        std::mutex mutex;
        std::condition_variable done;
    };
    struct Held {
        std::size_t index = 0;
        Node* node = nullptr; // alive: the cache's count is only given back by drop
    };

    // Object i held: past the budget, the least recently read dropped down to three quarters of
    // it. The lock of the budget, then those of the shards: never the other way.
    void hold(std::size_t i, Node* node) {
        std::vector<Node*> dropped; // their counts given back out of the locks
        {
            const std::lock_guard<std::mutex> lock(budgetLock_);
            node->used.store(clock_.fetch_add(1, std::memory_order_relaxed) + 1,
                             std::memory_order_relaxed);
            bytes_ += node->size;
            held_.push_back({i, node});
            if (bytes_ > budget_) {
                drop(dropped);
            }
        }
        for (Node* d : dropped) {
            release(d);
        }
    }

    // The least recently read dropped, down to three quarters of the budget, but the newest (the
    // last of held_).
    void drop(std::vector<Node*>& dropped) {
        const Held newest = held_.back();
        held_.pop_back();
        // The ticks read once: readers keep writing them.
        std::vector<std::pair<std::uint64_t, Held>> byUse;
        byUse.reserve(held_.size());
        for (const Held& h : held_) {
            byUse.emplace_back(h.node->used.load(std::memory_order_relaxed), h);
        }
        std::sort(byUse.begin(), byUse.end(), [](const auto& a, const auto& b) {
            return a.first < b.first || (a.first == b.first && a.second.index < b.second.index);
        });
        const std::size_t target = budget_ / 4 * 3;
        held_.clear();
        for (const auto& [tick, h] : byUse) {
            if (!(bytes_ > target)) {
                held_.push_back(h);
                continue;
            }
            {
                const std::lock_guard<std::mutex> lock(shards_[h.index % kShards].mutex);
                slots_[h.index] = nullptr;
            }
            bytes_ -= h.node->size;
            dropped.push_back(h.node);
        }
        held_.push_back(newest);
    }

    // Every waiting thread woken (raiseWorkStop): the locks of the shards are never held while an
    // object is made.
    static void wakeAll(void* self) noexcept {
        for (Shard& shard : static_cast<Lazy*>(self)->shards_) {
            const std::lock_guard<std::mutex> lock(shard.mutex);
            shard.done.notify_all();
        }
    }

    // The end of the making of object i: published (made), or its slot empty again (null).
    void finish(Shard& shard, std::size_t i, Node* made) {
        {
            const std::lock_guard<std::mutex> lock(shard.mutex);
            if (made != nullptr) {
                slots_[i] = made;
            }
            making_[i] = false;
        }
        shard.done.notify_all();
    }

    std::unique_ptr<Node*[]> slots_; // guarded by the lock of the shard, as making_
    std::unique_ptr<bool[]> making_;
    std::size_t count_ = 0;
    mutable std::array<Shard, kShards> shards_;
    std::atomic<std::uint64_t> clock_{0};
    std::atomic<std::size_t> made_{0};
    std::size_t budget_ = kUnbounded;
    Size size_ = nullptr;
    std::chrono::milliseconds step_ = kLazyWait;
    // Guarded by budgetLock_: the bytes held and what holds them.
    mutable std::mutex budgetLock_;
    std::size_t bytes_ = 0;
    std::vector<Held> held_;
    WorkStopWaking waking_{&Lazy::wakeAll, this}; // last: it goes before the shards
};

} // namespace terrain::water_detail
