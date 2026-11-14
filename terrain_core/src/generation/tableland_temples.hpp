#pragma once

// The temples of the canyons of the plateaus (agent 46, tableland_canyons.hpp), internal to
// terrain_core: buttes standing on the platform of a deep canyon, isolated from its walls and its
// inner gorge (Shiva, Isis, Zoroaster temples of the Grand Canyon). Each stands on a site of a
// jittered lattice of 1.4 km, moved across the canyon by up to 450 m as little as it takes to stand
// whole on the platform with its talus and a gap all around, no wider than the platform holds it
// there (it fades out where the platform is too narrow). Its plan: a radius of 120 to 300 m,
// stretched along a heading (its axes up to 1.7 to 1), barely bent by lobes, its outline moved by
// two noises (300 and 150 m) and the rough outline of the cliffs (tableland_cliffs.hpp): an
// irregular plan, never a flat side. Two storeys of cliff and talus: the upper one the same plan
// shrunk by a third to a half of the radius, at least its apron and a ledge inside the rim of the
// lower, so its top narrows. Their tops stand 10 to 30 % of the depth of the canyon under its rim.
// Pure arithmetic of doubles, noises and hashes: the same bits on every platform, periodic on the
// world.

#include "tableland_cliffs.hpp"
#include "tableland_fields.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace terrain::tableland_relief {

struct TempleNoises {
    noise::FbmOctaves jag; // the re-entrants of their outline
    noise::FbmOctaves jagFine;
    std::uint64_t siteSeed = 0;
    std::int64_t cellsX = 1; // of the lattice of their sites over the world
    std::int64_t cellsZ = 1;
    WorldSize world;
};

[[nodiscard]] TempleNoises makeTempleNoises(std::uint64_t seed, const WorldSize& world);

// The site of a temple: its center (near the column asking, maybe off the canonical world), its
// radius, how far it stretches along its heading (turns), its lobes, and its draws in [0, 1) (how
// far under the rim its top stands, the share of its lower storey, the width of its ledge, how far
// its upper storey is set back).
struct TempleSite {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    double stretch = 1.0;
    double heading = 0.0;
    std::array<double, 2> lobe{};
    std::array<double, 2> phase{};
    double drop = 0.0;
    double storey = 0.0;
    double ledge = 0.0;
    double setback = 0.0;
};

inline constexpr std::size_t kMostTempleSites = 9;

// The sites whose temple may reach a column (at most those of the 3 x 3 cells around it).
struct TempleSites {
    std::array<TempleSite, kMostTempleSites> site{};
    std::size_t count = 0;
};

// The sites whose temple may reach (x, z) in a canyon deep meters deep, its caprocks cliff of the
// height of each storey, its cliffs of slope steep.
[[nodiscard]] TempleSites templeSitesNear(const TempleNoises& n, double x, double z, double deep,
                                          double cliff, double steep);

// The canyon at the center of a site: its distance meters from the axis, the platform from from
// to to meters of it, depth meters under its rim where the platform lies (its inner edge), the
// canyon deep meters deep; (dirX, dirZ) the unit gradient of the distance to the axis.
struct TempleSpan {
    double distance = 0.0;
    double from = 0.0;
    double to = 0.0;
    double platform = 0.0;
    double deep = 0.0;
    double dirX = 1.0;
    double dirZ = 0.0;
};

// What the temples make of a column: the depth under the rim of the canyon of its ground there
// (their tops, cliffs, taluses and skirts), the gradient of that depth, its hard layer (none on a
// skirt), on a cliff.
struct Temple {
    bool stands = false;
    double outline = 0.0; // meters to the rim of a lower storey, positive within (their plan)
    double depth = 0.0;
    double gradX = 0.0;
    double gradZ = 0.0;
    Unit unit;
    bool cliff = false;
    bool skirt = false; // on its skirt: the ground of the canyon, raised (its floor, its rock)
};

// The temples of sites (spans: the canyon at each of their centers) at (x, z), their caprocks cliff
// of the height of each storey, their cliffs of slope steep and of the rough outline of walls
// (tableland_cliffs.hpp).
[[nodiscard]] Temple templeAt(const TempleNoises& n, double x, double z, const TempleSites& sites,
                              const std::array<TempleSpan, kMostTempleSites>& spans, double cliff,
                              double steep, const CliffNoises& walls);

} // namespace terrain::tableland_relief
