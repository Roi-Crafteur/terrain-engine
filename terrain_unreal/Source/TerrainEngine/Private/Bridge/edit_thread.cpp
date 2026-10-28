#include "edit_thread.hpp"

#include <utility>

namespace terrain::unreal {

EditThread::EditThread() : thread_([this] { loop(); }) {}

EditThread::~EditThread() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    thread_.join();
}

void EditThread::push(Job job) {
    {
        const std::lock_guard lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    ready_.notify_one();
}

std::vector<EditOutcome> EditThread::take() {
    const std::lock_guard lock(mutex_);
    return std::exchange(done_, {});
}

void EditThread::wait() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return jobs_.empty() && !running_; });
}

std::size_t EditThread::pending() const {
    const std::lock_guard lock(mutex_);
    return jobs_.size() + (running_ ? 1 : 0);
}

void EditThread::loop() {
    std::unique_lock lock(mutex_);
    for (;;) {
        ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_) {
            return;
        }
        Job job = std::move(jobs_.front());
        jobs_.pop_front();
        running_ = true;
        lock.unlock();
        EditOutcome outcome;
        try {
            outcome = job();
        } catch (...) {
            outcome.error = std::current_exception();
        }
        lock.lock();
        running_ = false;
        done_.push_back(std::move(outcome));
        idle_.notify_all();
    }
}

} // namespace terrain::unreal
