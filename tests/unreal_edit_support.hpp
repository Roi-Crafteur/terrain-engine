#pragma once

// Helpers of the tests of the edit thread of the session (Bridge/terrain_session.hpp): edits to
// queue or to apply on the synchronous path, the ground the synchronous path stages for them, and
// a pool of threads running the jobs of a horizon.

#include "terrain_edits.hpp"
#include "terrain_session.hpp"

#include "terrain/streaming/horizon_edit_columns.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace edit_test {

using terrain::unreal::GroundEdit;
using terrain::unreal::TerrainSession;

// A ground edit at (x, z) (its volume computed from the heights there), or a volume.
struct Edit {
    std::string label;
    std::optional<terrain::unreal::GroundEditCommand> ground;
    double x = 0.0;
    double z = 0.0;
    terrain::VolumeEdit volume;
};

// Edits around (x, z): a square razed, an edit refused (a box of no width), an explosion and a
// heap, then part of the square given back its generated ground.
inline std::vector<Edit> someEdits(const terrain::Heightfield& field, double x, double z) {
    const auto ground = [&field](double gx, double gz) {
        return static_cast<int>(std::floor(field.heightAt(gx, gz)));
    };
    std::vector<Edit> edits;
    edits.push_back({"raze",
                     terrain::unreal::GroundEditCommand{GroundEdit::Raze, 0.0, 200.0, 30.0},
                     x + 400.0,
                     z,
                     {}});
    edits.push_back({"refused",
                     std::nullopt,
                     0.0,
                     0.0,
                     {terrain::EditShape::Box,
                      terrain::EditMode::Remove,
                      {static_cast<int>(x), ground(x, z), static_cast<int>(z)},
                      {0, 10, 10},
                      0}});
    edits.push_back({"blast",
                     terrain::unreal::GroundEditCommand{GroundEdit::Blast, 0.0, 20.0, 10.0},
                     x + 400.0,
                     z + 100.0,
                     {}});
    edits.push_back({"heap",
                     terrain::unreal::GroundEditCommand{GroundEdit::Heap, 0.0, 30.0, 10.0},
                     x - 300.0,
                     z,
                     {}});
    edits.push_back(
        {"restore",
         std::nullopt,
         0.0,
         0.0,
         {terrain::EditShape::Box,
          terrain::EditMode::Restore,
          {static_cast<int>(x) + 300, ground(x + 400.0, z) - 200, static_cast<int>(z) - 100},
          {200, 400, 60},
          0}});
    return edits;
}

inline void queue(TerrainSession& session, const Edit& edit) {
    if (edit.ground) {
        session.queueGroundEdit(*edit.ground, edit.x, edit.z, edit.label);
    } else {
        session.queueEdit(edit.volume, edit.label);
    }
}

// An edit on the synchronous path of a session: the chunks it changed and the ground of their
// columns the session staged for the far horizon, or refused.
struct SyncEdit {
    std::vector<terrain::ChunkCoord> chunks;
    std::vector<terrain::EditedColumn> columns;
    bool refused = false;
};

inline SyncEdit applySync(TerrainSession& session, const Edit& edit) {
    const terrain::Heightfield& field = session.tiles().heightfield();
    SyncEdit result;
    try {
        result.chunks = session.applyEdit(
            edit.ground ? terrain::unreal::groundEditAt(field, *edit.ground, edit.x, edit.z)
                        : edit.volume);
    } catch (const std::invalid_argument&) {
        result.refused = true;
        return result;
    }
    result.columns = terrain::editedColumns(
        field, result.chunks, [&session](int minX, int maxX, int minZ, int maxZ) {
            return session.tiles().editWindow(minX, maxX, minZ, maxZ);
        });
    return result;
}

// The same columns, bit for bit.
inline bool sameColumns(const std::vector<terrain::EditedColumn>& a,
                        const std::vector<terrain::EditedColumn>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const terrain::EditedColumn& p, const terrain::EditedColumn& q) {
                          return p.column == q.column && p.edited == q.edited &&
                                 p.ground == q.ground;
                      });
}

// A pool of threads running jobs in their order; it must outlive the streamers launching on it.
class JobPool {
public:
    explicit JobPool(int threads) {
        for (int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { loop(); });
        }
    }
    ~JobPool() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        workers_.clear();
    }
    JobPool(const JobPool&) = delete;
    JobPool& operator=(const JobPool&) = delete;

    [[nodiscard]] std::function<void(std::function<void()>)> launcher() {
        return [this](std::function<void()> job) {
            {
                const std::lock_guard lock(mutex_);
                jobs_.push_back(std::move(job));
            }
            ready_.notify_one();
        };
    }

private:
    void loop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                return;
            }
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop_front();
            lock.unlock();
            job();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::vector<std::jthread> workers_; // last: joined first
};

} // namespace edit_test
