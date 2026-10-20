#pragma once

#include "math.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <functional>
#include <string>
#include <vector>

namespace viewer {

// How the cave districts show, computed on the CPU (no OpenGL): one color per district type, the
// tint that the mix of the districts under a vertex gives to the rock, and the text naming the
// mix. Only used by the world with cave regions.

// Share of the district mix in the color of the rock below the outer surface.
inline constexpr float kDistrictTint = 0.3f;

// Color of a district type, in [0, 1]: the one of the cave regions PNGs
// (terrain::kCaveDistrictRgb), so that the viewer and the images share their legend.
[[nodiscard]] Vec3 caveDistrictColor(terrain::CaveDistrictType type);

// The colors of the districts of a mix, weighted: the sum of weight * color of each district
// divided by the sum of the weights (1 for the weights of the core). Mid gray without weight.
[[nodiscard]] Vec3 districtMixColor(const terrain::DistrictWeights& weights);

// material + (mix - material) * strength: strength 0 keeps the material, 1 gives the mix.
[[nodiscard]] Vec3 tintColor(Vec3 material, Vec3 mix, float strength);

// The color of the district mix at the world position (x, z), meters.
using DistrictColorAt = std::function<Vec3(double x, double z)>;

// rockColors(data), with every vertex below the outer surface (any material but Surface) tinted
// by the district mix at its position, with the given strength; the outer surface keeps its
// altitude tint. mixAt is called on a coarse grid over the vertices of the tile (at most 81
// times), whose colors are interpolated bilinearly: the mix changes over hundreds of meters.
// Without mixAt or without materials, the colors are the ones of rockColors.
[[nodiscard]] std::vector<Vec3> districtRockColors(const terrain::TileData& data,
                                                   const DistrictColorAt& mixAt,
                                                   float strength = kDistrictTint);

// The mix as text: "Lush 70% / Dripstone 30%". Districts of the same type are added, the types
// are listed largest first (the nearest district first among equals), at most three, rounded to
// whole percents; a type rounding to 0% is left out. Empty without weights.
[[nodiscard]] std::string districtMixLabel(const terrain::DistrictWeights& weights);

} // namespace viewer
