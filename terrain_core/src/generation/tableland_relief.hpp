#pragma once

// The plateaus, mesas, buttes and canyons of the layered rock (agent 46, tablelands.hpp), internal
// to terrain_core: read by the Heightfield (heightfield_tablelands.cpp) where the provinces hold
// plateaus (TablelandPoint::tableland). Their shape before their rock (shapeAt):
//   - the escarpment: the zero line of a slow warped noise, biased by the share of the province
//     (the plateau within, the low ground without); its signed distance in meters from the
//     gradient of the noise;
//   - the outliers: beyond the escarpment, within the retreat of the province (far in the dry
//     lands, near in the humid ones), the blobs of a noise over a threshold rising with the
//     distance: promontories at the escarpment, then mesas, then buttes, then nothing;
//   - the profile: the top of each within its outline, its talus spreading outward, concave (steep
//     under the rim, gentle at the foot) at the slope of its style (steeper in the dry lands), down
//     to the low ground (the regional ground and its hills: drained, never a closed basin), carved
//     by gullies as deep as it is steep (the badlands of its soft layers);
//   - the canyons: the zero lines of a noise and their branches near them, deepest at the
//     escarpment, where their floor meets the low ground, rising inland (they drain outward);
//     narrowing out (box canyons) where they run along the escarpment or where a slow mask leaves
//     none; their branches hang over them.
// Their rock (passesAt): formations of 40 to 160 m, some hard (the caprocks and the cliffs: more of
// them in the dry lands), thin beds over them, laid as the strata of the walls of the mountains
// (mountain_walls::strataTop): a hard layer stands as a cliff over its apron of scree, a soft one
// keeps the slope; their faces in three dimensions are those of the walls (wall_faces::addFaces).
// None on the low ground nor on the floors of the canyons. The tops are never flat (topRelief).
// Pure arithmetic of doubles and noises: the same bits on every platform, periodic on the world.

#include "mountain_walls.hpp"
#include "ravines.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/tablelands.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::tableland_relief {

struct Noises {
    noise::FbmOctaves scarp;      // the escarpment
    noise::FbmOctaves scarpWarpX; // its warp
    noise::FbmOctaves scarpWarpZ;
    noise::FbmOctaves outliers;   // the mesas and buttes beyond it
    noise::FbmOctaves jags;       // the re-entrants and points of their rims
    noise::FbmOctaves canyons;    // the canyons
    noise::FbmOctaves branches;   // their branches
    noise::FbmOctaves meanderX;   // their entrenched meanders (a warp of their axes)
    noise::FbmOctaves meanderZ;
    noise::FbmOctaves canyonMask; // where they run
    noise::FbmOctaves thickness;  // of the plateau
    noise::FbmOctaves bands;      // the thickness of its formations
    noise::FbmOctaves dip;        // their offset
    noise::FbmOctaves rims;       // the recession of their rims and the run of their faces
    noise::FbmOctaves topFine;    // the relief of the tops
    noise::FbmOctaves topCoarse;
    std::uint64_t layers = 0;  // which formations are hard
    std::uint64_t beds = 0;    // and which beds
    std::uint64_t members = 0; // and which members (a second stack: no long soft run)
    ravines::Network gullies; // the gullies of the taluses and of the walls of the canyons
    WorldSize world;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// The share of the other forms of the relief (the mountains, the hills, the buttes and the forms of
// the plains) the plateaus take over at a point of the provinces p, in [0, 1]: all of it before
// the plateau rises (TablelandPoint::tableland 0.25), 0 out of the provinces.
[[nodiscard]] float calm(const TablelandPoint& p);

// The provinces at a point and the share of the plateaus kShareStep meters along x and along z
// (Tablelands::tablelandShare): the escarpment follows the edge of the province.
constexpr double kShareStep = 200.0;
struct Province {
    TablelandPoint point;
    float alongX = 0.0f;
    float alongZ = 0.0f;
};

// The fields of the plateau at a point, for the tests and the shape: distances in meters, positive
// within.
struct Fields {
    double thickness = 0.0; // meters of the plateau over the low ground there
    double scarp = 0.0;     // to the escarpment
    double outline = 0.0;   // to the outline of the top (the escarpment or an outlier)
    double talus = 0.0;     // meters the talus runs out from the outline
    double canyon = 0.0;    // meters the canyons cut the plateau
    double floor = 0.0;     // [0, 1]: on the floor of a canyon
    double profile = 0.0;   // [0, 1]: 0 on the low ground, 1 on the tops
};
[[nodiscard]] Fields fieldsAt(const Noises& n, double x, double z, const Province& q);

// The shape of the plateau at the canonical (x, z) of the provinces q, before its rock.
struct Shape {
    double rise = 0.0;  // meters the plateau raises the ground there
    double gradX = 0.0; // its gradient (meters per meter, up): the slope of the strata
    double gradZ = 0.0;
    double rock = 0.0;  // [0, 1]: the weight of the strata (none on the low ground and floors)
    double top = 0.0;   // [0, 1]: on a top (its relief, topRelief)
    double thickness = 0.0; // of the plateau over the low ground (Fields::thickness)
};
[[nodiscard]] Shape shapeAt(const Noises& n, double x, double z, const Province& q);

// The passes of the rock of the plateau at (x, z) of shape s over the regional ground regional (all
// of weight 0 where s.rock is 0): pass 0 its formations, pass 1 its beds, pass 2 its members
// (wall_faces::passesTop, addFaces). Its layers lie parallel to its top (regional + s.thickness),
// dipping gently: its tops are stripped surfaces on one layer (another one a few hundred
// kilometers away, past a cuesta), its escarpments and canyons cut through the layers below.
[[nodiscard]] mountain_walls::WallPasses passesAt(const Noises& n, double x, double z,
                                                  const TablelandPoint& p, const Shape& s,
                                                  double regional);

// The meters the relief of the tops adds there over the rock (never flat: swells of a few meters
// over a few kilometers, hummocks over a kilometer).
[[nodiscard]] double topRelief(const Noises& n, double x, double z, const Shape& s);

} // namespace terrain::tableland_relief
