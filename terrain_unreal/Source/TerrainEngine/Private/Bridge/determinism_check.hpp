#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace terrain::unreal {

// A height of the default world (kDefaultWorldSeed, HeightfieldParams{}): heightAt(x, z) bit for
// bit, as the core built by CMake computes it (checked by unreal_determinism_check_test).
struct HeightProbe {
    double x = 0.0;
    double z = 0.0;
    std::uint32_t bits = 0; // std::bit_cast<std::uint32_t>(heightAt(x, z))
};

// A tile of the default world (TerrainTiles(kDefaultWorldSeed), no caves): the fingerprint of
// its mesh, as the core built by CMake generates it. Heights are rounded to floats and hide most
// differences of floating point (fast math, contractions into FMA); the vertices of a mesh,
// interpolated between samples, show them.
struct TileProbe {
    TileRequest request;
    std::uint64_t fingerprint = 0; // tileFingerprint(generate(request))
};

// The probes: heights at the origin, on a slope, a mountain, the sea floor, and far points up to
// the size of the planet, with negative and fractional coordinates; LOD 0 tiles at the origin, on
// a mountain, and with seams.
// TODO(phase 9): the values of the terrain change with the wrapped world and the math functions
// of the core: update these tables in the same commit as the core (the ctest test fails until
// then, printing the new values).
[[nodiscard]] std::span<const HeightProbe> heightProbes();
[[nodiscard]] std::span<const TileProbe> tileProbes();

// FNV-1a (64 bits) of the bits of the origin, the vertices, the normals and the indices of the
// mesh of a tile: any difference of one bit changes it.
[[nodiscard]] std::uint64_t tileFingerprint(const TileData& data);

struct DeterminismReport {
    bool same = true;
    // With same false, the first probe that differs: a height (tile false) with the bits
    // computed instead, or a tile (tile true) with the fingerprint computed instead.
    bool tile = false;
    HeightProbe height;
    std::uint32_t actualBits = 0;
    TileProbe tileProbe;
    std::uint64_t actualFingerprint = 0;
};

// Compares the field, or the tiles, with the probes. They must be the default world for the
// probes to hold.
[[nodiscard]] DeterminismReport checkHeights(const Heightfield& field);
[[nodiscard]] DeterminismReport checkTiles(const TerrainTiles& tiles);

// The check of this build: the heights, then the tiles, of the default world against the probes,
// whatever the seed of the level. An Unreal build whose floating point differs from the core's
// generates another world; it must not stream.
[[nodiscard]] DeterminismReport checkDefaultWorld();

// For the log: "the heights and tiles ... match" or "this build does not generate the same
// world: ...".
[[nodiscard]] std::string describe(const DeterminismReport& report);

} // namespace terrain::unreal
