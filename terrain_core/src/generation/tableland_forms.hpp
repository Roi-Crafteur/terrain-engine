#pragma once

// The forms the provinces of the plateaus and of the karst lay on the relief (agent 46,
// tablelands.hpp), internal to terrain_core: read by the Heightfield (heightfield_tablelands.cpp).
// The high surfaces of the plateaus behind the collisions (their heart below the mountains, the
// geography keeping it so with GeographyParams::tablelands) carry inner ranges: long ranges of
// varied directions and lengths, along the zero lines of a slow noise warped strongly (no row, no
// lattice), broken by a mask, over about a seventh of the surface; the rest flat to rolling, its
// swells and basins and its hills. Pure arithmetic of floats and
// noises: the same bits on every platform, periodic on the world.

#include "terrain/generation/geography.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::tableland_forms {

struct Noises {
    noise::FbmOctaves rangeLines; // the zero lines of the inner ranges
    noise::FbmOctaves rangeWarpX; // their warp
    noise::FbmOctaves rangeWarpZ;
    noise::FbmOctaves rangeMask;  // which stretches of them stand
    noise::FbmOctaves swells;     // the swells and basins of the surfaces
    noise::FbmOctaves hills;      // and their hills
    WorldSize world;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// The share of the high surface of a plateau at a point of the geography g, in [0, 1]: the land
// high above the plains (kSurfaceLand), out of the belts and the rifts, of a relief below the
// mountains (the heart of a plateau, GeographyParams::tablelands). Only the plateaus behind the
// collisions rise so high out of the belts.
[[nodiscard]] float surfaceShare(const GeographySample& g);

// The intensity of the relief the inner ranges give the point (x, z), canonical, of a surface of
// share surface: up to kRangeRelief along the axes of the ranges, 0 between them.
[[nodiscard]] float innerRanges(const Noises& noises, double x, double z, float surface);

// The meters the swells, basins and hills of a surface of share surface add to its ground at
// (x, z), canonical: flat to rolling (a few hundred meters over tens of kilometers, tens of meters
// over a few), never a step.
[[nodiscard]] float surfaceRelief(const Noises& noises, double x, double z, float surface);

} // namespace terrain::tableland_forms
