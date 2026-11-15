#pragma once

// The plateaus, mesas, buttes and canyons of the layered rock (agent 46, tablelands.hpp), internal
// to terrain_core: read by the Heightfield (heightfield_tablelands.cpp) where the provinces hold
// plateaus (TablelandPoint::tableland). Their shape before their rock (shapeAt):
//   - the escarpment: the zero line of a slow warped noise, biased by the share of the province
//     (the plateau within, the low ground without), the edge of the province itself wandering at
//     every scale (Tablelands); its rim jagged by two noises; its signed distance in meters from
//     the gradient of the noise;
//   - the outliers: beyond the escarpment, within a retreat of 6 to 25 km (farther in the dry
//     lands), the blobs of two noises of 2.5 and 9 km mixed by a slow one, over a threshold rising
//     with the distance and moved by another slow one: promontories at the escarpment, fields of
//     buttes dense or sparse, mesas of several kilometers, then nothing;
//   - the profile of two storeys (tableland_fields.hpp: storeysAt): a caprock over a short concave
//     talus (Monument Valley: the De Chelly sandstone over the Organ Rock), the caprock 50 to 70 %
//     of the height of an outlier, at most kScarpCliffMost of an escarpment, the talus carved by
//     gullies, down to the low ground (the regional ground and its hills: drained); the caprock a
//     wall of rock (tableland_cliffs.hpp): its outline rough, battered, stepped by 2 to 4 hard
//     layers, scree at its foot;
//   - the canyons (tableland_canyons.hpp): a section of units, a caprock, slopes, a great cliff, a
//     platform with its temples, an inner gorge, a flat floor;
//   - the top: the thickness of the plateau swelling gently (the skyline of a rim is never a
//     straight line).
// Their rock (passesAt, tableland_strata.cpp): formations, members and beds of thicknesses of their
// own, laid as the strata of the walls of the mountains (mountain_walls::strataTop) parallel to the
// top, on the taluses and walls only; and the hard layer of each wall (its caprock, the great cliff,
// the caprock of a temple) forced as one layer. Their faces in three dimensions are those of the
// walls (wall_faces::addFaces). The tops are never flat (topRelief). Pure arithmetic of doubles and
// noises: the same bits on every platform, periodic on the world.

#include "mountain_walls.hpp"
#include "ravines.hpp"
#include "tableland_canyons.hpp"
#include "tableland_cliffs.hpp"
#include "tableland_front.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/tablelands.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::tableland_relief {

struct Noises {
    noise::FbmOctaves scarp;      // the escarpment
    noise::FbmOctaves scarpWarpX; // its warp
    noise::FbmOctaves scarpWarpZ;
    noise::FbmOctaves outliers;   // the buttes beyond it
    noise::FbmOctaves mesas;      // and the mesas
    noise::FbmOctaves outlierMix; // which of them, from place to place
    noise::FbmOctaves retreat;    // how far they reach
    noise::FbmOctaves density;    // how many they are
    noise::FbmOctaves bends;      // the great bends of the escarpment
    tableland_front::Swell bendSwell; // and their swell
    noise::FbmOctaves jags;       // the re-entrants and points of the rims
    noise::FbmOctaves jagsFine;
    noise::FbmOctaves cliffs;     // the share of the height the caprocks take
    CanyonNoises canyon;          // the canyons
    CliffNoises walls;            // what makes the caprocks walls of rock
    noise::FbmOctaves thickness;  // of the plateau
    noise::FbmOctaves swell;      // and its gentle swells
    noise::FbmOctaves bands;      // the thickness of its formations
    noise::FbmOctaves memberBands; // of its members
    noise::FbmOctaves bedBands;   // of its beds
    noise::FbmOctaves dip;        // the offset of its formations
    noise::FbmOctaves rims;       // the recession of their rims and the run of their faces
    noise::FbmOctaves topFine;    // the relief of the tops
    noise::FbmOctaves topCoarse;
    std::uint64_t layers = 0;  // which formations are hard
    std::uint64_t beds = 0;    // and which beds
    std::uint64_t members = 0; // and which members (a second stack: no long soft run)
    std::uint64_t units = 0;   // the draws of the forced hard layers
    ravines::Network gullies; // the gullies of the taluses and of the walls of the canyons
    WorldSize world;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// The share of the other forms of the relief (the mountains, the hills, the buttes and the forms of
// the plains) the plateaus take over at a point of the provinces p, in [0, 1]: all of it before
// the plateau rises (TablelandPoint::tableland 0.1), 0 out of the provinces.
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
    double talus = 0.0;     // meters the cliff and the talus run out from the outline
    double cliff = 0.0;     // meters of the caprock of that outline
    double height = 0.0;    // meters of its top over the low ground
    double canyon = 0.0;    // meters the canyons cut the plateau
    double floor = 0.0;     // [0, 1]: on the floor of a canyon
    double profile = 0.0;   // [0, 1]: 0 on the low ground, 1 on the tops
    double axis = 0.0;      // meters from the axis of the nearest canyon (signed)
    double gorge = 0.0;     // meters from the edge of its inner gorge, positive within
    bool outlier = false;   // the outline is that of an outlier
    bool temple = false;    // a temple of a canyon stands there
    double templeOutline = 0.0; // meters to the rim of its lower storey, positive within
};
[[nodiscard]] Fields fieldsAt(const Noises& n, double x, double z, const Province& q);

// The shape of the plateau at the canonical (x, z) of the provinces q, before its rock.
struct Shape {
    double rise = 0.0;  // meters the plateau raises the ground there
    double gradX = 0.0; // its gradient (meters per meter, up): the slope of the strata
    double gradZ = 0.0;
    double rock = 0.0;  // [0, 1]: the weight of the strata (none on the low ground and floors)
    double top = 0.0;   // [0, 1]: on a top (its relief, topRelief; no strata but its caprock)
    double thickness = 0.0; // of the plateau over the low ground (Fields::thickness)
    // [0, 1]: out in front of the escarpment, where the outliers stand on soft slopes (the Organ Rock
    // under the De Chelly sandstone of Monument Valley): few hard formations and members there.
    double soft = 0.0;
    // The hard layer of the wall at the column, forced (its caprock, the great cliff of a canyon,
    // the caprock of a temple): its top in meters over the low ground (as rise), its height, its
    // weight (1 on the wall and the top over it, 0 on the talus under it).
    double unitTop = 0.0;
    double unitHeight = 0.0;
    double unitWeight = 0.0;
    // [0, 1]: the wall and the top over it, whatever the share of the province (the weight of the
    // hard layer without the fade of the rock): the other strata keep off it.
    double unitCover = 0.0;
    // The great cliff of a canyon is one hard layer (the Redwall, 120 to 200 m); a caprock 2 to 4
    // (tableland_cliffs.hpp: layersAt).
    bool unitWhole = false;
    // Over the great cliff of a canyon, the top of that cliff (meters over the low ground, as rise; 0:
    // none): the soft layers of the upper walls recede down to it, never into it.
    double floorTop = 0.0;
    // Where a canyon lowers the escarpment, its caprock with it: the rise of the floor the canyon
    // leaves behind the rim and how much of a top that floor is (as Shape::top; rise 0: none). The
    // rim never stands over the rock of that floor (heightfield_tablelands.cpp): no lip along it.
    struct Lowered {
        double rise = 0.0;
        double top = 0.0;
    };
    Lowered lowered;
};
[[nodiscard]] Shape shapeAt(const Noises& n, double x, double z, const Province& q);

// The passes of the rock of the plateau at (x, z) of shape s over the ground base (the low ground
// there, the regional ground and its hills): pass 0 its formations, pass 1 its beds, pass 2 its
// members, laid parallel to its top (base + s.thickness), on its taluses and walls only; pass 3 the
// hard layer of the wall (Shape::unitTop), forced (wall_faces::passesTop, addFaces).
[[nodiscard]] mountain_walls::WallPasses passesAt(const Noises& n, double x, double z,
                                                  const TablelandPoint& p, const Shape& s,
                                                  double base);

// The lowest bound of the forced hard layers of a caprock (Shape::unitTop, not unitWhole) over its
// foot, over base: under it their lowest layer is partial, and the rock keeps to the shape (its
// batter) rather than lay its ledge under the foot.
[[nodiscard]] double unitBench(const Noises& n, double x, double z, const Shape& s, double base);

// The meters the relief of the tops adds there over the rock (never flat: swells of a few meters
// over a few kilometers, hummocks over a kilometer).
[[nodiscard]] double topRelief(const Noises& n, double x, double z, const Shape& s);

} // namespace terrain::tableland_relief
