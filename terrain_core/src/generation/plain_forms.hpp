#pragma once

// The forms of the plains of the planetary geography (phase 11R bis, step 5c), internal to
// terrain_core: what a plain shows at the scale of a walk and of its horizon, where the mountains
// are not (the Heightfield adds them only where the mountain mask is 0, through a weight that
// vanishes before the mask rises: the mountains keep their bits).
//
// The kind of plain changes from one region of a few hundred kilometers to the next: some almost
// flat (the Beauce), some rolling (the Palouse), most in between. It scales every form below,
// and the relief of relief_local (hills, vallons, buttes) under it:
// - long swells of 5 to 10 km, bumps on the drained ground only (no closed hollow of that size:
//   the low land between them holds together, the water passes);
// - hills of 300 m to 1 km, and a micro relief of 30 to 100 m;
// - vallons along the zero lines of a warped noise of one octave: sinuous, unevenly spaced (1 to
//   2 km), 100 to 300 m wide, 8 to 25 m deep, deeper here and shallower there, in the low land
//   between the swells; they head on the crests of the hills and fade where their lines would
//   close in rings; shorter branches, here and there, that join them;
// - now and then an isolated hill of 50 to 150 m in a cell of about 7 km: a short ridge of 2 to 4
//   rounded summits of unequal heights with saddles between them, an irregular outline, one
//   flank longer than the other, knolls and shoulders, its own hills and micro relief stronger,
//   gullies down the outer part of its flanks; the vallons and the relief of relief_local end at
//   its foot.
// Deterministic, periodic on the world, the same bits through the memos.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::plain_forms {

struct Noises {
    noise::FbmOctaves kind;     // the kind of plain, regions of a few hundred kilometers
    noise::FbmOctaves swells;   // 5 to 10 km
    noise::FbmOctaves hills;    // 300 m to 1 km
    noise::FbmOctaves micro;    // 30 to 100 m
    noise::FbmOctaves warpX;    // the outlines of the vallons and of the isolated hills
    noise::FbmOctaves warpZ;
    noise::FbmOctaves vallons;  // their zero lines
    noise::FbmOctaves branches; // those of the branches
    noise::FbmOctaves presence; // where the branches are, how deep the vallons
    noise::FbmOctaves knolls;   // on the isolated hills
    std::uint64_t hillSeed = 0;
    int cellsX = 1; // cells of the isolated hills along each side of the world
    int cellsZ = 1;
    WorldSize world;
};

struct Memos {
    noise::FbmOctaves::Memo kind;
    noise::FbmOctaves::Memo swells;
    noise::FbmOctaves::Memo hills;
    noise::FbmOctaves::Memo micro;
    noise::FbmOctaves::Memo warpX;
    noise::FbmOctaves::Memo warpZ;
    noise::FbmOctaves::Memo vallons;
    noise::FbmOctaves::Memo branches;
    noise::FbmOctaves::Memo presence;
    noise::FbmOctaves::Memo knolls;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// What the plain is made of at a point.
struct Ground {
    float weight = 0.0f; // (0, 1]: the share of the forms of the plains (none at 0)
    float region = 0.0f; // the regions of the map: lands of vallons above 0
    float local = 0.0f;  // the relief of relief_local at the point, meters
};

// The kind of plain at (x, z), from 0 (flat) to 1 (rolling).
[[nodiscard]] float kindAt(const Noises& noises, double x, double z, Memos* memos);

// The height of the isolated hills at (x, z), meters (0 away from them), weight 1.
[[nodiscard]] double hillAt(const Noises& noises, double x, double z, Memos* memos);

// The depth of the vallons at (x, z), meters, as deep as they go: in the most rolling plains, in
// the lands of vallons, off the swells and the isolated hills.
[[nodiscard]] double vallonsAt(const Noises& noises, double x, double z, Memos* memos);

// The relief of the plain at (x, z), canonical: the local relief, scaled by the kind of plain,
// plus the forms of the plains, all of it weight of the way from the local relief alone.
// memos may be null.
[[nodiscard]] float relief(const Noises& noises, double x, double z, const Ground& ground,
                           Memos* memos);

} // namespace terrain::plain_forms
