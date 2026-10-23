#pragma once

#include "tile_streamer.hpp"
#include "tile_timings.hpp"
#include "unreal_frame.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace terrain::unreal {

struct SessionParams {
    std::uint64_t seed = kDefaultWorldSeed;
    bool caves = false;       // the caves of CaveParams{} under the heightfield
    bool caveRegions = false; // with caves only: their regions (CaveDistrictParams{})
    WorldSize world;          // the wrapped world, of the tiles and of the streaming
    StreamParams stream;      // its world is replaced by world
    // The water of the world (phase 11B): the planet level of the seed in this file (written by
    // terrain_make_planet, storage/planet_file.hpp), built and written there when it does not
    // hold it; its hydrology carves the rivers and fills the lakes of the tiles and the horizon.
    // Empty: the relief alone, without rivers or lakes. UTF-8.
    std::string planetFile;
};

// The streamed world of an Unreal level: its tiles, the timings of their generations and
// conversions, and their streamer. Members are destroyed in reverse order, so the workers of the
// streamer stop (once their current tile is generated) before the tiles they read go away.
class TerrainSession {
public:
    // Throws std::invalid_argument for an invalid world, like TileStreamer for invalid stream
    // parameters.
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
