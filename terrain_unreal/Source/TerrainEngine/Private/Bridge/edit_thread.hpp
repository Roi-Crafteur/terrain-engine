#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edit_columns.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace terrain::unreal {

// What an edit done on the edit thread gives back to the thread owning the session.
struct EditOutcome {
    std::string label;                 // given with the edit, for the logs
    VolumeEdit volume;                 // the volume applied (a volume or ground edit)
    std::vector<ChunkCoord> chunks;    // the chunks it changed, to generate again
    std::vector<EditedColumn> columns; // their ground for the far horizon (editedColumns)
    std::exception_ptr error;          // the edit refused: nothing changed
    // On the edit thread: the edit of the masks (with the volume of a ground edit), then the
    // ground of the columns; on the owning thread: its tiles invalidated and its ground staged.
    double editMilliseconds = 0.0;
    double columnMilliseconds = 0.0;
    double stageMilliseconds = 0.0;
};

// One thread doing the edits of a session one after another (phase 15): a large edit takes
// hundreds of milliseconds to write into the masks of the tiles and to read back as the ground of
// the columns of the far horizon, which the game thread must not wait for. The jobs run in the
// order they were pushed, their outcomes are taken in that order; push, take and wait belong to
// the thread owning the queue. A job may throw: its outcome then holds the error.
class EditThread {
public:
    using Job = std::function<EditOutcome()>;

    EditThread();
    // The job running ends; the jobs not started are dropped.
    ~EditThread();

    EditThread(const EditThread&) = delete;
    EditThread& operator=(const EditThread&) = delete;

    void push(Job job);
    // The outcomes of the jobs done since the last take, in their order.
    [[nodiscard]] std::vector<EditOutcome> take();
    // Until every job pushed is done (their outcomes wait for take).
    void wait();
    // Jobs pushed and not done.
    [[nodiscard]] std::size_t pending() const;

private:
    void loop();

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::condition_variable idle_;
    std::deque<Job> jobs_;
    std::vector<EditOutcome> done_;
    bool running_ = false;
    bool stopping_ = false;
    std::jthread thread_; // last member: joined before the others are destroyed
};

} // namespace terrain::unreal
