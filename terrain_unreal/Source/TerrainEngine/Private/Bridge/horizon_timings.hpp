#pragma once

#include "tile_timings.hpp"

#include "terrain/streaming/horizon_levels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terrain::unreal {

// What is uploaded for a level: the body and the trim of its mesh (horizon_layout.hpp), or the
// texels of the GPU horizon (horizon_textures.hpp).
enum class HorizonSection : std::uint8_t { Body, Trim, Texels };
inline constexpr std::size_t kHorizonSectionCount = 3;

// What the far horizon cost, on the game thread unless stated. StageStats counts measures in
// its field tiles.
struct HorizonTimingsReport {
    // The ticks of the horizon (update, conversions, uploads, moves): the one that showed every
    // level first, those that applied an epoch by the coarsest level it moved, and the others.
    StageStats firstShow;
    std::array<StageStats, kHorizonMaxLevels> moves{};
    StageStats idle;
    // Per section uploaded: its conversion by the bridge and its upload into Unreal, with the
    // vertices of the meshes.
    std::array<StageStats, kHorizonSectionCount> conversion{};
    std::array<StageStats, kHorizonSectionCount> upload{};
    std::array<std::size_t, kHorizonSectionCount> vertices{};
    // The epochs committed, from their plan to their commit (computed by the workers), and the
    // samples they computed.
    StageStats epochs;
    std::int64_t samples = 0;
};

// Measures of the far horizon, recorded by the thread owning it (not thread-safe).
class HorizonTimings {
public:
    // A tick of the horizon: coarsestMoved is the coarsest level its epoch moved, -1 without an
    // epoch; first when it showed the levels for the first time. Throws std::invalid_argument for
    // a level out of [-1, kHorizonMaxLevels).
    void recordTick(int coarsestMoved, bool first, double milliseconds);
    void recordUpload(HorizonSection section, double conversionMilliseconds,
                      double uploadMilliseconds, std::size_t vertices);
    void recordEpoch(double milliseconds, std::int64_t samples);

    [[nodiscard]] const HorizonTimingsReport& snapshot() const { return report_; }
    // The measures since the previous take (or the start), then starts again from zero.
    HorizonTimingsReport take();

private:
    HorizonTimingsReport report_;
};

// Lines for the log, only for what was measured, for example
//   horizon ticks: first show 1 tick 85.20 ms, level 0 moves 12 ticks 4.10 ms (max 5.20), ...
//   horizon body: conversion 13 meshes 1.90 ms (max 2.40), upload 13 meshes 2.20 ms (max 3.10),
//   55432 vertices per mesh
//   horizon texels: upload 12 ticks 0.02 ms (max 0.05), 514 texels per tick
//   horizon epochs: 12 epochs 9.30 ms (max 21.00) from plan to commit, 9252 samples
[[nodiscard]] std::vector<std::string> describe(const HorizonTimingsReport& report);

} // namespace terrain::unreal
