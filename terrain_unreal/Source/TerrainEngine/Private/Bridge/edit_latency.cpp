#include "edit_latency.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

namespace {

// The value at the fraction q of the sorted values (nearest rank).
template <typename T>
T rank(std::vector<T> values, double q) {
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(q * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

} // namespace

std::uint64_t EditLatency::clicked(double seconds, std::int64_t frame) {
    Waiting edit;
    edit.id = next_++;
    edit.clickSeconds = seconds;
    edit.clickFrame = frame;
    waiting_.push_back(std::move(edit));
    return waiting_.back().id;
}

void EditLatency::done(std::uint64_t id, std::span<const ChunkCoord> chunks, double seconds) {
    const auto it = std::find_if(waiting_.begin(), waiting_.end(),
                                 [id](const Waiting& edit) { return edit.id == id; });
    if (it == waiting_.end()) {
        return;
    }
    if (chunks.empty()) {
        waiting_.erase(it);
        return;
    }
    it->done = true;
    it->doneSeconds = seconds;
    it->chunks.assign(chunks.begin(), chunks.end());
}

std::size_t EditLatency::uploaded(const TileKey& tile, const WorldSize& world, double seconds,
                                  std::int64_t frame) {
    if (tile.lod != 0) {
        return 0;
    }
    const TileKey canonical = wrapTile(tile, world);
    std::size_t shown = 0;
    for (auto it = waiting_.begin(); it != waiting_.end();) {
        const bool over = it->done && std::any_of(it->chunks.begin(), it->chunks.end(),
                                                  [&](const ChunkCoord& chunk) {
                                                      return tileContains(
                                                          canonical, wrapChunk(chunk, world));
                                                  });
        if (!over) {
            ++it;
            continue;
        }
        timings_.push_back({it->doneSeconds - it->clickSeconds, seconds - it->clickSeconds,
                            frame - it->clickFrame});
        if (timings_.size() > kept_) {
            timings_.pop_front();
        }
        it = waiting_.erase(it);
        ++shown;
    }
    return shown;
}

LatencySummary EditLatency::summary() const {
    LatencySummary summary;
    summary.count = timings_.size();
    if (timings_.empty()) {
        return summary;
    }
    std::vector<double> done;
    std::vector<double> shown;
    std::vector<std::int64_t> frames;
    for (const EditTiming& timing : timings_) {
        done.push_back(timing.doneSeconds * 1000.0);
        shown.push_back(timing.shownSeconds * 1000.0);
        frames.push_back(timing.frames);
    }
    summary.doneMedian = rank(done, 0.5);
    summary.shownMedian = rank(shown, 0.5);
    summary.shownP95 = rank(shown, 0.95);
    summary.shownMax = rank(shown, 1.0);
    summary.framesMedian = rank(frames, 0.5);
    summary.framesMax = rank(frames, 1.0);
    return summary;
}

} // namespace terrain::unreal
