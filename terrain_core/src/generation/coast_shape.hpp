#pragma once

// The ground of the geography along its coasts, internal to terrain_core: the coastal plain and the
// shelf of phase 11R (moved here from heightfield_geography.cpp, bit for bit), and the band where
// the coasts of agent 33 shape them (coast.hpp).

#include "coast_kinds.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/geography_params.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::coast_shape {

// Whether a point of coast signal coast lies in the band of the coasts: from kCoastSeaBand of
// signal at sea to coastRamp inland, both excluded.
[[nodiscard]] bool inBand(const GeographyParams& p, double coast);

// The factor of the land at a coast signal: the mountains, the hills and the forms of the plains
// fade in from the coast over coastRamp (smoothstep), 0 at sea.
[[nodiscard]] float landFactor(const GeographyParams& p, double coast);

// The sea floor near the coast at a coast signal coast < 0: down to the shelf depth over the
// shelf width, then the slope.
[[nodiscard]] double shelf(const GeographyParams& p, double coast);

// Ground of the geography g before the local relief at the coast signal coast: the shelf and the
// ocean floor below the coast, the coastal plain then the regional elevation above it. Below the
// sea level exactly where coast is negative: both sides strictly of their sign, 0 on the coast.
[[nodiscard]] float groundBase(const GeographyParams& p, const GeographySample& g, double coast);

// The steepest slope (meters per meter) of a face height meters high that the tiles draw without
// teeth (marching cubes on y - h(x, z), normals by differences of the density, cells of 1 to 8 m
// at the LOD 0 to 3): measured on synthetic faces (coast_patterns_test), it depends on the height
// of the face in cells only. In cells of the LOD 3 (8 m): 0.75 up to 2.5 cells, 1.25 at 5, 2 at
// 10, 3 from 20 on, interpolated on the logarithm of the height. Below 2.5 cells of the LOD 3
// (20 m) the limit stays 0.75, clean at the LODs whose cells are at most half the height of the
// face (LOD 0 to 2 from 8 m on); the LOD 3 of those faces, 256 m away and more, keeps a residue.
[[nodiscard]] double faceSlopeLimit(double height);

// The noises of the shapes of the coasts, each varying at its own scale (no regular pattern along
// a coast): the height of the faces (a slow, contrasted noise and a medium one), the width of the
// rock platforms at their foot, the width of their faces, the edge (capes and hollows of their
// top) and the screes at their foot.
struct Noises {
    noise::FbmOctaves stretch;
    noise::FbmOctaves height;
    noise::FbmOctaves platform;
    noise::FbmOctaves face;
    noise::FbmOctaves edge;
    noise::FbmOctaves scree;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// What the coasts make of the ground at a point of their band (agent 33): the factors of the land
// (the hills and the forms of the plains, and the mountains, fade in from the shore) and the
// ground before the local relief. Each kind has its own; they are blended by their weights, and
// give way to the low coast over the last 10 km of signal of the band. The low coasts keep
// landFactor and groundBase; the rocky coasts, the cliffs, the calanques and the rias stand up
// from a rock platform as a face no steeper than faceSlopeLimit (screes at its foot, capes and
// hollows along its top) to a plateau from a few meters to their highest along the coast, the
// hills and vallons of the hinterland on it; the mountains plunge into the sea, rising steepest at
// the shore. Out at sea, the floor drops faster at the foot of the high coasts, steepest at the
// shore, down to the shelf at the outer edge of the band. distance: the distance to the shore in
// meters (the coast signal over its gradient), signed as the signal.
struct Ground {
    float land = 0.0f;
    float mountains = 0.0f; // never above land
    float ground = 0.0f;
};

[[nodiscard]] Ground shapeAt(const GeographyParams& p, const Noises& noises, double x, double z,
                             const GeographySample& g, double coast, double distance,
                             const coast_kinds::Weights& weights);

} // namespace terrain::coast_shape
