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
#include <limits>

namespace terrain::coast_profile {
struct Shore;
} // namespace terrain::coast_profile

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

// The blocks of the walls of the coasts (their buttresses and recesses): jittered cells about
// kBlockCell meters wide over the world (as many as fit along each side), each drawn by seed
// (coast_profile::shoreAt).
inline constexpr double kBlockCell = 150.0;
struct Blocks {
    std::uint64_t seed = 0;
    std::int64_t cellsX = 1;
    std::int64_t cellsZ = 1;
    WorldSize world;
};

// The noises of the shapes of the coasts, each varying at its own scale (no regular pattern along
// a coast): the height of the faces (a slow, contrasted noise and a medium one), the width of the
// rock platforms at their foot, the width of their faces, the edge (capes and hollows of their
// top) and the screes at their foot; along the walls, read at the shore (coast_profile::Along),
// the meanders of their line, their blocks and the dips of their top.
struct Noises {
    noise::FbmOctaves stretch;
    noise::FbmOctaves height;
    noise::FbmOctaves platform;
    noise::FbmOctaves face;
    noise::FbmOctaves edge;
    noise::FbmOctaves scree;
    noise::FbmOctaves meander;
    noise::FbmOctaves dip;
    Blocks blocks;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// What the coasts make of the ground at a point of their band (agent 33): the factors of the land
// (the hills and the forms of the plains, and the mountains, fade in from the shore) and the
// ground before the local relief. Each kind has its own; they are blended by their weights, and
// give way to the low coast over the last 10 km of signal of the band. The low coasts keep
// landFactor and groundBase; the rocky coasts, the cliffs, the calanques and the rias stand up
// from a rock platform as a face no steeper than faceSlopeLimit (screes at its foot, capes and
// hollows along its top) to a plateau from a few meters to their highest along the coast, the
// hills and vallons of the hinterland on it; along the cliffs and the rocky coasts, their wall
// moves in and out (meanders and blocks) and its top dips (coast_profile::wallFaceAt); the
// mountains plunge into the sea, rising steepest at the shore. Out at sea, the floor drops faster
// at the foot of the high coasts, steepest at the shore, down to the shelf at the outer edge of
// the band. distance: the distance to the shore in meters (the coast signal over its gradient),
// signed as the signal.
struct Ground {
    float land = 0.0f;
    float mountains = 0.0f; // never above land
    float ground = 0.0f;
    // The share of the local relief its terraces step before the fade from the shore: 1 on the
    // low coasts (their slow fade, as without the coasts), land on the others (their quick one).
    float fade = 1.0f;
    // How irregular its terraces are: the share of the high coasts, 0 on the low coasts and at
    // the inner edge of the band (relief_local::Shape::irregular).
    float irregular = 0.0f;
    // Bare rock on the surface, 0 or 1: the rocks of the reefs and the sea kRockMargin meters
    // around them, the rock platforms and the faces in three dimensions of the rocky coasts, the
    // cliffs and the calanques (HeightSample::rock).
    float rock = 0.0f;
};

// What the inlets and stacks make of the point (coast_inlets::Effect): the coast signal of the
// geography (coast in shapeAt is that of the world, the inlets cut in it), the floor of the inlets
// (at most 0), the share of the plunging mountains on their sides, the dome of the stacks, the
// depth of the floor of the inlets on their axis and how far into their water the point lies (as
// rise): under their water the plunging mountains go on down to that depth (no crease at the
// water).
struct Inlets {
    double base = 0.0;
    double floor = 0.0;
    double rise = 1.0;
    double dome = 0.0;
    double deep = 0.0;
    double sink = 0.0;
    // In the reach of the fjords, how far the plunging mountains rise from their water rather
    // than from the shore of the geography (coast_inlets::Effect::glen).
    double glen = 0.0;
    // Into the rocks of the reefs, meters (coast_inlets::Effect::islet, below 0 out of them).
    double islet = -std::numeric_limits<double>::infinity();
    // That rock (coast_inlets::Effect::rockX and the next ones): read by its faces only.
    double rockX = 0.0;
    double rockZ = 0.0;
    double rockRadius = 0.0;
    double rockSize = 0.0;
    double rockHeight = 0.0;
    double rockTop = 0.0;
    double rockPhase = 0.0;
    // Into the next rock (coast_inlets::Effect::nextIslet).
    double nextIslet = -std::numeric_limits<double>::infinity();
    // The stack whose spire may stand over the point (coast_inlets::Effect::spireX and the next
    // ones).
    double spireX = 0.0;
    double spireZ = 0.0;
    double spireSize = 0.0;
    double spireHeight = 0.0;
};

// Whether the kinds weights read the noises of a shore (coast_profile::shoreAt): the rocky coasts,
// the cliffs, the calanques, the rias and the fjords.
[[nodiscard]] bool readsShore(const coast_kinds::Weights& weights);

// shore: the noises of the shore at the point (coast_profile::shoreAt; none needed unless
// readsShore), the same for every distance read at the point.
[[nodiscard]] Ground shapeAt(const GeographyParams& p, const GeographySample& g, double coast,
                             double distance, const coast_kinds::Weights& weights,
                             const Inlets& inlets, const coast_profile::Shore& shore);

// How high the shore of the kinds weights stands at (x, z), a point of the shore of the geography
// g, 400 m inland: the top of the faces of the shore kinds (as shapeAt raises them there), the
// plunging kinds that far up their rise, the low coasts 0; blended by their weights. The coves and
// the stacks keep to shores that rise (coast_lattice).
[[nodiscard]] double shoreTop(const Noises& noises, double x, double z, const GeographySample& g,
                              const coast_kinds::Weights& weights);

} // namespace terrain::coast_shape
