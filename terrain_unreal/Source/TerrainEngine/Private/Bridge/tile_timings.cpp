#include "tile_timings.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

namespace {

std::size_t lodIndex(int lod) {
    if (lod < 0 || lod >= kLodCount) {
        throw std::invalid_argument("TileTimings: LOD out of range");
    }
    return static_cast<std::size_t>(lod);
}

constexpr std::array<const char*, kTileStageCount> kStageNames{"generation", "conversion",
                                                               "upload"};

std::string describeStage(const char* name, const StageStats& stats) {
    char text[128];
    std::snprintf(text, sizeof(text), "%s %zu tiles %.2f ms (max %.2f)", name, stats.tiles,
                  stats.meanMilliseconds(), stats.maxMilliseconds);
    return text;
}

std::string describeLod(int lod, const LodTimings& timings) {
    std::string line = "LOD " + std::to_string(lod) + ":";
    for (std::size_t i = 0; i < kTileStageCount; ++i) {
        line += (i == 0 ? " " : ", ") + describeStage(kStageNames[i], timings.stages[i]);
    }
    if (timings.meshes > 0) {
        char text[96];
        std::snprintf(text, sizeof(text), ", %zu vertices %zu triangles per mesh",
                      timings.vertices / timings.meshes, timings.triangles / timings.meshes);
        line += text;
    }
    return line;
}

void addTo(StageStats& stats, double milliseconds) {
    ++stats.tiles;
    stats.totalMilliseconds += milliseconds;
    stats.maxMilliseconds = std::max(stats.maxMilliseconds, milliseconds);
}

bool holdsMeasures(const LodTimings& timings) {
    return timings.meshes > 0 ||
           std::any_of(timings.stages.begin(), timings.stages.end(),
                       [](const StageStats& stats) { return stats.tiles > 0; });
}

} // namespace

double StageStats::meanMilliseconds() const {
    return tiles == 0 ? 0.0 : totalMilliseconds / static_cast<double>(tiles);
}

const StageStats& LodTimings::stage(TileStage which) const {
    return stages[static_cast<std::size_t>(which)];
}

void TileTimings::record(TileStage stage, int lod, double milliseconds) {
    const std::size_t index = lodIndex(lod);
    const std::lock_guard lock(mutex_);
    for (TimingsReport* report : {&report_, &totals_}) {
        addTo((*report)[index].stages[static_cast<std::size_t>(stage)], milliseconds);
    }
}

void TileTimings::recordMesh(int lod, std::size_t vertices, std::size_t triangles) {
    const std::size_t index = lodIndex(lod);
    const std::lock_guard lock(mutex_);
    for (TimingsReport* report : {&report_, &totals_}) {
        LodTimings& timings = (*report)[index];
        ++timings.meshes;
        timings.vertices += vertices;
        timings.triangles += triangles;
    }
}

void TileTimings::recordWater(int lod, double milliseconds) {
    const std::size_t index = lodIndex(lod);
    const std::lock_guard lock(mutex_);
    for (TimingsReport* report : {&report_, &totals_}) {
        addTo((*report)[index].water, milliseconds);
    }
}

TimingsReport TileTimings::snapshot() const {
    const std::lock_guard lock(mutex_);
    return report_;
}

TimingsReport TileTimings::take() {
    const std::lock_guard lock(mutex_);
    return std::exchange(report_, TimingsReport{});
}

TimingsReport TileTimings::totals() const {
    const std::lock_guard lock(mutex_);
    return totals_;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed =
        std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

TileGenerator timedGenerator(TileGenerator generator, std::shared_ptr<TileTimings> timings) {
    if (!generator || timings == nullptr) {
        throw std::invalid_argument("timedGenerator: a generator and timings expected");
    }
    return [generator = std::move(generator),
            timings = std::move(timings)](const TileRequest& request) {
        const auto start = std::chrono::steady_clock::now();
        TileData data = generator(request);
        timings->record(TileStage::Generation, request.key.lod, millisecondsSince(start));
        return data;
    };
}

std::vector<std::string> describe(const TimingsReport& report) {
    std::vector<std::string> lines;
    for (std::size_t lod = 0; lod < report.size(); ++lod) {
        if (holdsMeasures(report[lod])) {
            lines.push_back(describeLod(static_cast<int>(lod), report[lod]));
        }
    }
    return lines;
}

} // namespace terrain::unreal
