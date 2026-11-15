#include "ground_read.hpp"

#include "terrain/work_stop.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace terrain::unreal {

namespace {

bool finished(const std::future<GroundLevel>& result) {
    return result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
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
    // Every read stopped; then the futures wait for them, which leave by WorkStopped.
    forget();
    for (Read& read : forgotten_) {
        raiseWorkStop(*read.stop);
    }
}

void GroundRead::ask(const TerrainTiles& tiles, double x, double z) {
    forget();
    current_.stop = std::make_shared<std::atomic<bool>>(false);
    current_.result =
        std::async(std::launch::async, [&tiles, x, z, stop = current_.stop] {
            const WorkStopScope scope(stop.get());
            return groundLevelAt(tiles, x, z);
        });
}

void GroundRead::forget() {
    dropFinished();
    if (current_.result.valid()) {
        raiseWorkStop(*current_.stop); // nobody reads it: it stops at once
        forgotten_.push_back(std::move(current_));
        current_ = Read{};
    }
}

std::optional<GroundLevel> GroundRead::poll() {
    dropFinished();
    if (!current_.result.valid() || !finished(current_.result)) {
        return std::nullopt;
    }
    return current_.result.get(); // the future is empty afterwards
}

std::size_t GroundRead::running() const {
    std::size_t count = current_.result.valid() && !finished(current_.result) ? 1 : 0;
    for (const Read& read : forgotten_) {
        count += finished(read.result) ? 0 : 1;
    }
    return count;
}

void GroundRead::dropFinished() {
    std::erase_if(forgotten_, [](const Read& read) { return finished(read.result); });
}

} // namespace terrain::unreal
