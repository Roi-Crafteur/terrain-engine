#pragma once

// Helpers of the cave flora tests: the lush districts of the default world, caves whose every
// district is lush (to control the climate, the water and the ice), the placement over squares,
// and the checks of one plant against the carved density around it.

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_flora.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"

#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "speleothems_test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace flora_test {

using caves_test::terrainDensity;
using speleothems_test::samplerOver;
using speleothems_test::Square;
using terrain::CaveColumn;
using terrain::CaveField;
using terrain::CaveFlora;
using terrain::CaveFloraField;
using terrain::CaveFloraKind;
using terrain::CaveFloraParams;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;

// The seed of the lush districts below, found in the default world.
constexpr std::uint64_t kSeed = 1337;

struct Site {
    double x;
    double z;
};

// Sites of lush districts with caves (under the sea, with lakes): 5 x 5 tiles of LOD 0 around
// each hold 20 to 90 plants.
inline constexpr std::array<Site, 4> kLushSites = {
    {{26012.0, -22376.0}, {25080.0, -9885.0}, {-19282.0, -1731.0}, {-16612.0, 4990.0}}};

inline CaveParams districtParams() {
    CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    return params;
}

// Caves whose every district has the same lush profile (lush and ice in [0, 1]): with a ground of
// the same height everywhere, whose environment is the climate, they control the climate, the
// water (the aquifers are all wet or all dry) and the ice of the plants.
inline CaveParams lushWorld(bool wet, float lush = 1.0f, float ice = 0.0f) {
    CaveParams params = speleothems_test::aquiferParams(wet);
    CaveProfile profile;
    profile[CaveProfile::kLush] = lush;
    profile[CaveProfile::kIce] = ice;
    terrain::CaveDistrictParams districts;
    districts.profiles.fill(profile);
    params.districts = districts;
    return params;
}

inline CaveGround flatWorld(const terrain::CaveEnvironment& environment) {
    return caves_test::withEnvironment(caves_test::flatGround(300.0f), environment);
}

inline terrain::CaveEnvironment mildEnvironment() {
    return caves_test::environmentOf(terrain::Biome::TemperateForest, 16.0f, 0.8f);
}

// Square of the given side centered on (x, z).
inline Square around(double x, double z, double side) {
    return {x - side / 2.0, z - side / 2.0, side};
}

inline std::vector<CaveFlora> placeIn(const CaveFloraField& field, CaveSampler& sampler,
                                      const Square& s) {
    return field.place(sampler, s.minX, s.minZ, s.minX + s.size, s.minZ + s.size);
}

// The plants of a square from a sampler of its own, with the flora parameters of the field.
inline std::vector<CaveFlora> plantsOver(const CaveField& field, const CaveGround& ground,
                                         const Square& s, std::uint64_t seed = kSeed) {
    CaveSampler sampler = samplerOver(field, ground, s);
    const CaveFloraParams params =
        field.districts() != nullptr ? field.districts()->params().flora : CaveFloraParams{};
    return placeIn(CaveFloraField(seed, params), sampler, s);
}

inline std::vector<CaveFlora> sorted(std::vector<CaveFlora> list) {
    std::sort(list.begin(), list.end(), [](const CaveFlora& a, const CaveFlora& b) {
        return std::make_tuple(a.z, a.x, static_cast<int>(a.kind), a.y, a.size) <
               std::make_tuple(b.z, b.x, static_cast<int>(b.kind), b.y, b.size);
    });
    return list;
}

inline bool contains(const std::vector<CaveFlora>& list, const CaveFlora& plant) {
    return std::find(list.begin(), list.end(), plant) != list.end();
}

// True if every plant of `part` is in `whole`.
inline bool subsetOf(const std::vector<CaveFlora>& part, const std::vector<CaveFlora>& whole) {
    return std::all_of(part.begin(), part.end(),
                       [&whole](const CaveFlora& plant) { return contains(whole, plant); });
}

// What the checks of the plants found.
struct PlantChecks {
    std::size_t plants = 0;
    std::size_t ferns = 0;
    std::size_t mushrooms = 0;
    std::size_t vines = 0;
    std::size_t wet = 0;        // plants over a lake
    std::size_t offBase = 0;    // base not within 0.3 m of the surface (see checkPlant)
    std::size_t badBase = 0;    // base not within 0.6 m of the surface
    std::size_t badAir = 0;     // rock somewhere along the plant
    std::size_t badSize = 0;    // out of [minSize, maxSize]
    std::size_t badShare = 0;   // taller than maxShare of the free height
    std::size_t underWater = 0; // a base or a tip under the water of its column
    std::size_t notLush = 0;    // where the lush field of the profile is not above 0
    std::size_t icy = 0;
    std::size_t frozen = 0;
    std::size_t inEntrance = 0; // in the column of an entrance
    double tightest = 0.0;      // largest size / (maxShare * free height)
};

// The carved density along the vertical of a plant, and its direction from the rock it grows on
// toward the air (up for a fern or a mushroom, down for a vine).
struct Vertical {
    CaveSampler& sampler;
    CaveColumn column;
    double base;
    double toAir;

    Vertical(CaveSampler& s, const CaveFlora& plant)
        : sampler(s), column(s.column(plant.x, plant.z, s.ground().height(plant.x, plant.z))),
          base(static_cast<double>(plant.y)),
          toAir(plant.kind == CaveFloraKind::Vine ? -1.0 : 1.0) {}

    [[nodiscard]] float atHeight(double y) {
        return sampler.density(column, y, terrainDensity(y, column.ground));
    }

    // Carved density at `t` meters from the base toward the air.
    [[nodiscard]] float at(double t) { return atHeight(base + toAir * t); }

    // True if the base stands on rock with air in front of it, `reach` meters each side.
    [[nodiscard]] bool standsOnRock(double reach) { return at(-reach) < 0.0f && at(reach) > 0.0f; }

    // Height of the free room, in meters, from the base to the opposite rock: read from the
    // density on the lattice of whole meters, with the crossing of 0 interpolated between the
    // last sample in air and the first in rock, as the mesh sees it (a rock thinner than the
    // lattice is not part of the mesh).
    [[nodiscard]] double freeHeight() {
        double y = toAir > 0.0 ? std::floor(base) + 1.0 : std::ceil(base) - 1.0;
        float previous = atHeight(y);
        for (int step = 0; step < 60 && previous >= 0.0f; ++step) {
            const float current = atHeight(y + toAir);
            if (current < 0.0f) {
                const double crossing =
                    y + toAir * static_cast<double>(previous / (previous - current));
                return std::abs(crossing - base);
            }
            previous = current;
            y += toAir;
        }
        return previous >= 0.0f ? 60.0 : std::abs(y - base);
    }
};

// Checks the plant against the carved density of its column, and its column:
// - the base stands on rock with air in front of it, within 0.6 m of the surface (the same
//   tolerance as the speleothems: the surface is found from the density sampled every meter, so
//   it is off by up to 0.07 m on the walls of the lush districts of the world and 0.33 m on a
//   flat ceiling of the synthetic caves), and within 0.3 m for all but a few plants (offBase);
// - the whole plant is in air;
// - its size is in range and within maxShare of the free height (0.05 m more);
// - it is not under the water of its column, its profile is lush and not icy, its column not
//   frozen and not the column of an entrance.
inline void checkPlant(CaveSampler& sampler, const CaveFlora& plant, PlantChecks& out) {
    const CaveFloraParams& p = sampler.field().districts()->params().flora;
    Vertical v(sampler, plant);
    const bool vine = plant.kind == CaveFloraKind::Vine;
    const auto size = static_cast<double>(plant.size);
    ++out.plants;
    out.ferns += plant.kind == CaveFloraKind::Fern ? 1 : 0;
    out.mushrooms += plant.kind == CaveFloraKind::Mushroom ? 1 : 0;
    out.vines += vine ? 1 : 0;
    out.offBase += v.standsOnRock(0.3) ? 0 : 1;
    out.badBase += v.standsOnRock(0.6) ? 0 : 1;
    bool air = true;
    for (double t = 0.4; t <= size; t += 0.1) {
        air = air && v.at(t) >= 0.0f;
    }
    out.badAir += air ? 0 : 1;
    out.badSize += plant.size >= p.minSize - 1.0e-4f && plant.size <= p.maxSize + 1.0e-4f ? 0 : 1;

    // A vine hangs over the water: the free height stops there.
    const bool wet = v.column.waterLevel > terrain::kDryLevel;
    double free = v.freeHeight();
    if (vine && wet) {
        free = std::min(free, v.base - static_cast<double>(v.column.waterLevel));
    }
    const double share = static_cast<double>(p.maxShare);
    out.badShare += size <= share * (free + 0.05) ? 0 : 1;
    out.tightest = std::max(out.tightest, size / (share * free));
    const double lowest = vine ? v.base - size : v.base;
    out.wet += wet ? 1 : 0;
    out.underWater += wet && lowest < static_cast<double>(v.column.waterLevel) ? 1 : 0;

    const CaveProfile profile = sampler.profileAt(plant.x, plant.z);
    out.notLush += profile[CaveProfile::kLush] > 0.0f ? 0 : 1;
    out.icy += profile.icy() ? 1 : 0;
    out.frozen += v.column.frozen ? 1 : 0;
    const auto& entrances = sampler.entrances();
    const auto margin = static_cast<double>(p.entranceMargin);
    out.inEntrance += std::any_of(entrances.begin(), entrances.end(),
                                  [&](const terrain::CaveEntrance& e) {
                                      return terrain::entranceColumn(e, plant.x, plant.z, margin);
                                  })
                          ? 1
                          : 0;
}

inline PlantChecks checkAll(CaveSampler& sampler, const std::vector<CaveFlora>& list) {
    PlantChecks checks;
    for (const CaveFlora& plant : list) {
        checkPlant(sampler, plant, checks);
    }
    return checks;
}

} // namespace flora_test
