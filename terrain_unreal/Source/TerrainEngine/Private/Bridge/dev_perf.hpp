#pragma once

#include "tile_timings.hpp"

#include "terrain/streaming/lod.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace terrain::unreal {

// The page of the performance of F3 (agent 36): the frames and the tiles over a sliding window of
// time, whatever the summaries of the log take of the timings (TileTimings::totals).

// The times of one frame, milliseconds: its length, and the game thread, the render thread and
// the GPU as stat unit gives them.
struct FrameTimes {
    double frame = 0.0;
    double game = 0.0;
    double render = 0.0;
    double gpu = 0.0;
};

struct MeanMax {
    double mean = 0.0;
    double max = 0.0;
};

// What the window holds: the frames per second and the means and maxima of their times; per
// LOD, the tiles generated per second and the mean of their generation, conversion and upload;
// the tiles with water and the mean time of their water meshes.
struct PerfSummary {
    double window = 0.0;  // the length of the window, seconds
    double seconds = 0.0; // between the totals of the tiles kept: the window, once it is full
    double framesPerSecond = 0.0;
    MeanMax frame;
    MeanMax game;
    MeanMax render;
    MeanMax gpu;
    std::array<double, kLodCount> tilesPerSecond{};
    std::array<double, kLodCount> generationMs{};
    std::array<double, kLodCount> conversionMs{};
    std::array<double, kLodCount> uploadMs{};
    std::size_t waterTiles = 0;
    double waterMs = 0.0;
};

class PerfWindow {
public:
    // Throws std::invalid_argument unless windowSeconds > 0.
    explicit PerfWindow(double windowSeconds = 5.0);

    // A frame ending at now (seconds, never decreasing).
    void addFrame(double now, const FrameTimes& times);
    // The totals of the timings of the tiles at now (TileTimings::totals).
    void addTiles(double now, const TimingsReport& totals);
    // Forgets everything (another world, Terrain.Seed).
    void clear();

    [[nodiscard]] PerfSummary summary() const;

private:
    struct Frame {
        double time = 0.0;
        FrameTimes times;
    };
    struct Tiles {
        double time = 0.0;
        TimingsReport totals{};
    };

    double window_;
    std::deque<Frame> frames_;
    std::deque<Tiles> tiles_;
};

// The lines of the page, for example "fps 58.2, frame 17.18 ms (max 25.10)",
// "LOD 0: 12.4 tiles/s, generation 3.10 ms, conversion 0.21 ms, upload 1.30 ms".
[[nodiscard]] std::vector<std::string> describe(const PerfSummary& summary);

} // namespace terrain::unreal
