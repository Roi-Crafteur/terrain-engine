#pragma once

#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace terrain::unreal {

// The stages of the cost of a tile: its generation by terrain_core (on a worker thread), its
// conversion into the arrays of Unreal, and its upload into an Unreal mesh.
enum class TileStage : std::uint8_t { Generation, Conversion, Upload };
inline constexpr std::size_t kTileStageCount = 3;

struct StageStats {
    std::size_t tiles = 0;
    double totalMilliseconds = 0.0;
    double maxMilliseconds = 0.0;

    [[nodiscard]] double meanMilliseconds() const;
};

// What the tiles of one LOD cost.
struct LodTimings {
    std::array<StageStats, kTileStageCount> stages;
    // Converted meshes and their sizes.
    std::size_t meshes = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    // The water meshes of the tiles that have some (lakes, rivers, falls), within their
    // conversion: their tiles and the time to build them (the tools of the developers; not in
    // describe).
    StageStats water;

    [[nodiscard]] const StageStats& stage(TileStage stage) const;
};

using TimingsReport = std::array<LodTimings, kLodCount>;

// Measures of the tiles, per LOD. Thread-safe: the workers record the generations. Each record
// throws std::invalid_argument for a LOD outside [0, kLodCount).
class TileTimings {
public:
    void record(TileStage stage, int lod, double milliseconds);
    void recordMesh(int lod, std::size_t vertices, std::size_t triangles);
    void recordWater(int lod, double milliseconds);

    [[nodiscard]] TimingsReport snapshot() const;
    // The measures since the previous take (or the start), then starts again from zero.
    TimingsReport take();
    // Every measure since the start, which take never resets: the rates over a window of time
    // of the tools of the developers (dev_perf.hpp), whatever the summaries of the log take.
    [[nodiscard]] TimingsReport totals() const;

private:
    mutable std::mutex mutex_;
    TimingsReport report_{};
    TimingsReport totals_{};
};

// Milliseconds elapsed since start, on the steady clock.
[[nodiscard]] double millisecondsSince(std::chrono::steady_clock::time_point start);

// The generator, each generation timed on TileStage::Generation; the tiles are unchanged.
// Throws std::invalid_argument for an empty generator or null timings.
[[nodiscard]] TileGenerator timedGenerator(TileGenerator generator,
                                           std::shared_ptr<TileTimings> timings);

// One line per LOD holding measures, for the log: the tiles of each stage with their mean and
// maximum time, and the mean size of the converted meshes, for example
//   LOD 0: generation 12 tiles 35.10 ms (max 80.20), conversion 12 tiles 0.21 ms (max 0.40),
//   upload 12 tiles 1.30 ms (max 3.10), 2100 vertices 4000 triangles per mesh
[[nodiscard]] std::vector<std::string> describe(const TimingsReport& report);

} // namespace terrain::unreal
