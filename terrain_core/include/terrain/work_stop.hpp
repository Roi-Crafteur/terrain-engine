#pragma once

// Stopping a long computation from another thread (agent 39): the game closing, the dev map
// stopping, a new seed. A thread doing work that may be stopped holds a WorkStopScope with a flag;
// the long computations of the core (the courses of the rivers, the fine lakes: water/) call
// workStopPoint() every few milliseconds, which throws WorkStopped once the flag of the thread is
// raised. What they were making is then dropped whole: the caches only keep what was finished
// (water/hydrology_lazy.hpp), so the next query computes it again, the same bits.
//
// The flag is the thread's: one thread stopped (the dev map) leaves the others (the tiles)
// computing. A thread without a scope is never stopped. Header only: the pointer of the thread
// lives in a function-local thread_local of an inline function, one per program (terrain_core is
// one library, and in the plugin one module).

#include <atomic>
#include <stdexcept>

namespace terrain {

// Thrown by workStopPoint() on a thread whose flag is raised. Whoever installed the scope catches
// it before any other exception: the work is dropped, nothing went wrong.
class WorkStopped : public std::runtime_error {
public:
    WorkStopped() : std::runtime_error("work stopped") {}
};

namespace work_stop_detail {

[[nodiscard]] inline const std::atomic<bool>*& flagOfThread() noexcept {
    thread_local const std::atomic<bool>* flag = nullptr;
    return flag;
}

} // namespace work_stop_detail

// While it lives, the work of this thread stops once *flag is raised (null: never). Scopes nest:
// the previous flag is given back at the end.
class WorkStopScope {
public:
    explicit WorkStopScope(const std::atomic<bool>* flag) noexcept
        : previous_(work_stop_detail::flagOfThread()) {
        work_stop_detail::flagOfThread() = flag;
    }
    ~WorkStopScope() { work_stop_detail::flagOfThread() = previous_; }
    WorkStopScope(const WorkStopScope&) = delete;
    WorkStopScope& operator=(const WorkStopScope&) = delete;

private:
    const std::atomic<bool>* previous_;
};

// The flag of this thread (null without a scope): to hand it to the threads of a parallel loop.
[[nodiscard]] inline const std::atomic<bool>* workStopFlag() noexcept {
    return work_stop_detail::flagOfThread();
}

// Whether the flag of this thread is raised.
[[nodiscard]] inline bool workStopRaised() noexcept {
    const std::atomic<bool>* flag = work_stop_detail::flagOfThread();
    return flag != nullptr && flag->load(std::memory_order_relaxed);
}

// Throws WorkStopped when the flag of this thread is raised; nothing otherwise.
inline void workStopPoint() {
    if (workStopRaised()) {
        throw WorkStopped();
    }
}

} // namespace terrain
