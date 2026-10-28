#pragma once

// Helpers of the tests of the GPU horizon of the Unreal plugin (Bridge/horizon_gpu_streamer): what
// the GPU holds (the textures the texel regions write, the placements of the levels), the
// reference of the texels shown, jobs run when the test says so and a pool of threads.

#include "horizon_gpu_streamer.hpp"

#include "terrain/streaming/horizon_sampler.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace gpu_test {

using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::HorizonTexel;
using terrain::unreal::kTexelFloats;
using terrain::unreal::LevelPlacement;

// What the GPU of Unreal holds: a texture and a placement per level, the regions written in order.
class Gpu {
public:
    explicit Gpu(int side) : side_(side) {}

    void apply(const HorizonGpuUpdate& update) {
        for (const terrain::unreal::TexelRegion& region : update.regions) {
            std::vector<float>& texels = textures_[region.level];
            texels.resize(static_cast<std::size_t>(side_ * side_ * kTexelFloats), -9.0f);
            for (int row = 0; row < region.height; ++row) {
                for (int column = 0; column < region.width * kTexelFloats; ++column) {
                    texels[static_cast<std::size_t>(((region.z + row) * side_ + region.x) *
                                                        kTexelFloats +
                                                    column)] =
                        region.data[static_cast<std::size_t>(row * region.width * kTexelFloats +
                                                             column)];
                }
            }
        }
        for (const LevelPlacement& placement : update.placements) {
            placements_[placement.level] = placement;
        }
    }

    // The texel the material reads for the sample (i, k) of the window of a level.
    [[nodiscard]] HorizonTexel texel(int level, int i, int k) const {
        const LevelPlacement& placement = placements_.at(level);
        const auto x = static_cast<int>((placement.firstSlot.x + i) % side_);
        const auto z = static_cast<int>((placement.firstSlot.z + k) % side_);
        const std::vector<float>& texels = textures_.at(level);
        const auto first = static_cast<std::size_t>((z * side_ + x) * kTexelFloats);
        return {texels[first], texels[first + 1], texels[first + 2], texels[first + 3]};
    }

    // The same windows read by the material, the same origins and trims.
    [[nodiscard]] bool sameView(const Gpu& other) const {
        for (const auto& [level, placement] : placements_) {
            const LevelPlacement& theirs = other.placements_.at(level);
            if (!(placement.origin == theirs.origin) || placement.trim != theirs.trim ||
                !(placement.inCoarser == theirs.inCoarser)) {
                return false;
            }
            for (int k = 0; k < side_; ++k) {
                for (int i = 0; i < side_; ++i) {
                    if (!(texel(level, i, k) == other.texel(level, i, k))) {
                        return false;
                    }
                }
            }
        }
        return placements_.size() == other.placements_.size();
    }

    [[nodiscard]] int side() const { return side_; }
    [[nodiscard]] const std::map<int, LevelPlacement>& placements() const { return placements_; }
    // The texture of a level, written by the regions of the updates applied.
    [[nodiscard]] std::vector<float>& texture(int level) { return textures_.at(level); }

private:
    int side_;
    std::map<int, std::vector<float>> textures_;
    std::map<int, LevelPlacement> placements_;
};

// The texels of every window of a clipmap as a sampler computes their samples (with the crests in
// the epochs: the reference of what the GPU shows once the detail is in).
inline bool texelsMatch(const Gpu& gpu, const terrain::HorizonClipmap& clipmap,
                        const terrain::HorizonSampler& reference) {
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        const std::vector<terrain::HorizonCell> cells = reference.sample(n, level.window());
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                const terrain::HorizonCell& cell =
                    cells[static_cast<std::size_t>(k * level.side() + i)];
                if (!(gpu.texel(n, i, k) == terrain::unreal::horizonTexel(cell, true))) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Jobs run when the test says so.
struct Jobs {
    std::deque<std::function<void()>> queue;

    void runAll() {
        while (!queue.empty()) {
            std::function<void()> job = std::move(queue.front());
            queue.pop_front();
            job();
        }
    }
    [[nodiscard]] terrain::unreal::JobLauncher launcher() {
        return [this](std::function<void()> job) { queue.push_back(std::move(job)); };
    }
};

// Jobs run by `threads` threads, first come first served.
class Pool {
public:
    explicit Pool(int threads) {
        for (int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { work(); });
        }
    }
    ~Pool() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        workers_.clear();
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    [[nodiscard]] terrain::unreal::JobLauncher launcher() {
        return [this](std::function<void()> job) {
            {
                const std::lock_guard lock(mutex_);
                jobs_.push_back(std::move(job));
            }
            ready_.notify_one();
        };
    }

private:
    void work() {
        std::unique_lock lock(mutex_);
        while (true) {
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

} // namespace gpu_test
