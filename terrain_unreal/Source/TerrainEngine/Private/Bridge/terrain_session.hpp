#pragma once

#include "tile_streamer.hpp"
#include "tile_timings.hpp"
#include "unreal_frame.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstdint>
#include <memory>

namespace terrain::unreal {

struct SessionParams {
    std::uint64_t seed = kDefaultWorldSeed;
    bool caves = false;       // the caves of CaveParams{} under the heightfield
    bool caveRegions = false; // with caves only: their regions (CaveDistrictParams{})
    StreamParams stream;
};

// The streamed world of an Unreal level: its tiles, the timings of their generations and
// conversions, and their streamer. Members are destroyed in reverse order, so the workers of the
// streamer stop (once their current tile is generated) before the tiles they read go away.
class TerrainSession {
public:
    // Throws like TileStreamer for invalid stream parameters.
    explicit TerrainSession(const SessionParams& params);

    TerrainSession(const TerrainSession&) = delete;
    TerrainSession& operator=(const TerrainSession&) = delete;

    [[nodiscard]] const TerrainTiles& tiles() const { return tiles_; }
    [[nodiscard]] TileTimings& timings() { return *timings_; }
    [[nodiscard]] TileStreamer& streamer() { return streamer_; }

    // The frame that places the ground of the core point (x, z) at the Unreal origin.
    [[nodiscard]] UnrealFrame frameAt(double x, double z) const;

    // Unreal Z of the ground (Heightfield::heightAt) under an Unreal position, centimeters.
    [[nodiscard]] double groundZ(const UnrealFrame& frame, const UnrealPoint& point) const;

private:
    TerrainTiles tiles_;
    std::shared_ptr<TileTimings> timings_;
    TileStreamer streamer_; // last: destroyed first
};

} // namespace terrain::unreal
