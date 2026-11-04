#include "ground_read.hpp"

#include "terrain/work_stop.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace terrain::unreal {

namespace {

bool finished(const std::future<GroundLevel>& read) {
    return read.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

} // namespace

GroundLevel groundLevelAt(const TerrainTiles& tiles, double x, double z) {
    const auto start = std::chrono::steady_clock::now();
    const HeightSample sample = tiles.heightfield().sampleAt(x, z);
    GroundLevel level;
    level.x = x;
    level.z = z;
    level.ground = static_cast<double>(tiles.groundAt(x, z));
    level.stand = std::max(level.ground, static_cast<double>(sample.waterLevel));
    level.milliseconds = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    return level;
}

GroundRead::~GroundRead() {
    raiseWorkStop(stop_); // then the futures wait for their reads, which leave by WorkStopped
}

void GroundRead::ask(const TerrainTiles& tiles, double x, double z) {
    forget();
    current_ = std::async(std::launch::async, [&tiles, x, z, stop = &stop_] {
        const WorkStopScope scope(stop);
        return groundLevelAt(tiles, x, z);
    });
}

void GroundRead::forget() {
    dropFinished();
    if (current_.valid()) {
        forgotten_.push_back(std::move(current_));
    }
}

std::optional<GroundLevel> GroundRead::poll() {
    dropFinished();
    if (!current_.valid() || !finished(current_)) {
        return std::nullopt;
    }
    return current_.get(); // the future is empty afterwards
}

std::size_t GroundRead::running() const {
    std::size_t count = current_.valid() && !finished(current_) ? 1 : 0;
    for (const std::future<GroundLevel>& read : forgotten_) {
        count += finished(read) ? 0 : 1;
    }
    return count;
}

void GroundRead::dropFinished() {
    std::erase_if(forgotten_, [](const std::future<GroundLevel>& read) { return finished(read); });
}

} // namespace terrain::unreal
