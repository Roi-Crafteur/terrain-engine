#pragma once

// The ground of a teleport read on a thread (agent 38). The first read of a region builds its
// hydrology (202 ms measured in Unreal at the foot of a chain): it must not hold the game thread.
// The pawn is held meanwhile; it is placed once the level is read. The level to stand at is the
// ground, or the surface of the water of the core over it (a lake, a river, the sea) when it is
// higher: a pawn placed there is never under the water (standLevel). The ground is the top of the rock the
// tiles draw (TerrainTiles::groundAt): behind a face in three dimensions (a wall of the mountains,
// a sea cliff) the relief of the heightmap lies up to half its height below it, in the rock.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water_kind.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstddef>
#include <future>
#include <optional>
#include <vector>

namespace terrain::unreal {

// The ground of the core point (x, z) and the level to stand at there, meters; the time of the
// read, milliseconds.
struct GroundLevel {
    double x = 0.0;
    double z = 0.0;
    double ground = 0.0;
    double stand = 0.0;
    double milliseconds = 0.0;
};

// The level to stand at over a column whose ground is given: the ground on dry land, whatever
// HeightSample::waterLevel holds there (the height of the relief: on the floor of a valley of the
// planet, 56 m over the ground the tiles draw, where #58 placed the eye and the pawn; agent 41);
// over the water of the core (the sea, a lake, a river: water not None), the higher of the
// ground and its surface.
[[nodiscard]] double standLevel(double ground, const HeightSample& sample);

// On the calling thread: TerrainTiles::groundAt, and the level to stand at over it (standLevel of
// Heightfield::sampleAt).
[[nodiscard]] GroundLevel groundLevelAt(const TerrainTiles& tiles, double x, double z);

class GroundRead {
public:
    GroundRead() = default;
    GroundRead(const GroundRead&) = delete;
    GroundRead& operator=(const GroundRead&) = delete;
    ~GroundRead() = default; // waits for the reads still running (the tiles must outlive them)

    // Reads the level of (x, z) on a thread; a read still running is forgotten, never waited for.
    void ask(const TerrainTiles& tiles, double x, double z);
    // The read running is forgotten (a load that reads nothing begins).
    void forget();
    [[nodiscard]] bool pending() const { return current_.valid(); }
    // Once read, the level of the last point asked, given once; nothing before, nor after.
    [[nodiscard]] std::optional<GroundLevel> poll();
    // The reads still running, forgotten ones included.
    [[nodiscard]] std::size_t running() const;

private:
    void dropFinished();

    std::future<GroundLevel> current_;
    std::vector<std::future<GroundLevel>> forgotten_;
};

} // namespace terrain::unreal
