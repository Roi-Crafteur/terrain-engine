#pragma once

// Helpers of the speleothem tests: squares of the world to place, a sampler over each, the checks
// of the anchoring of each speleothem in the rock and the air around it, and the columns that
// open to the sky.

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/speleothems.hpp"

#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <vector>

namespace speleothems_test {

using caves_test::flatGround;
using caves_test::kSeed;
using caves_test::terrainDensity;
using caves_test::withEnvironment;
using terrain::CaveColumn;
using terrain::CaveDetail;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveSampler;
using terrain::kDryLevel;
using terrain::Speleothem;
using terrain::SpeleothemField;
using terrain::SpeleothemKind;
using terrain::SpeleothemParams;

constexpr float kFlatHeight = 300.0f;

// Square [minX, minX + size) x [minZ, minZ + size).
struct Square {
    double minX;
    double minZ;
    double size;
};

// A place of the default world rich in caves under the flat ground, around which the tests lay
// their squares (found by search for the seed of the tests: the caves of a square depend on
// where it lies).
constexpr double kSiteX = 512.0;
constexpr double kSiteZ = -512.0;

// Square of side size centered on the site.
inline Square aroundSite(double size) {
    return {kSiteX - size / 2.0, kSiteZ - size / 2.0, size};
}

// Sampler whose box holds the columns of the square, from maxDepth + 2 m under their ground to
// their ground.
inline CaveSampler samplerOver(const CaveField& field, const CaveGround& ground, const Square& s) {
    float low = ground.height(s.minX, s.minZ);
    float high = low;
    for (double z = s.minZ; z <= s.minZ + s.size; z += 2.0) {
        for (double x = s.minX; x <= s.minX + s.size; x += 2.0) {
            low = std::min(low, ground.height(x, z));
            high = std::max(high, ground.height(x, z));
        }
    }
    constexpr double kSlack = 4.0;
    const double depth = static_cast<double>(field.params().maxDepth) + 2.0;
    return CaveSampler(field, ground, CaveDetail{},
                       {s.minX, static_cast<double>(low) - depth - kSlack, s.minZ, s.minX + s.size,
                        static_cast<double>(high) + kSlack, s.minZ + s.size});
}

inline std::vector<Speleothem> placeIn(const SpeleothemField& field, CaveSampler& sampler,
                                       const Square& s) {
    return field.place(sampler, s.minX, s.minZ, s.minX + s.size, s.minZ + s.size);
}

inline std::vector<Speleothem> sorted(std::vector<Speleothem> list) {
    std::sort(list.begin(), list.end(), [](const Speleothem& a, const Speleothem& b) {
        return std::make_tuple(a.z, a.x, static_cast<int>(a.kind)) <
               std::make_tuple(b.z, b.x, static_cast<int>(b.kind));
    });
    return list;
}

inline std::size_t countKind(const std::vector<Speleothem>& list, SpeleothemKind kind) {
    return static_cast<std::size_t>(std::count_if(
        list.begin(), list.end(), [kind](const Speleothem& s) { return s.kind == kind; }));
}

inline std::size_t countIce(const std::vector<Speleothem>& list) {
    return static_cast<std::size_t>(
        std::count_if(list.begin(), list.end(), [](const Speleothem& s) { return s.ice; }));
}

// Parameters with the aquifers of every land cell wet (all == true) or dry.
inline CaveParams aquiferParams(bool all) {
    CaveParams params;
    params.aquifers.dryChance = all ? 1.0f : 0.0f;
    params.aquifers.wetChance = all ? 1.0f : 0.0f;
    return params;
}

// Floor of the void whose ceiling is at `ceiling` in the column: where the carved density
// crosses 0 between the lattice samples at integer heights, going down, as the placement finds
// it.
inline float floorUnder(CaveSampler& sampler, const CaveColumn& column, float ceiling) {
    const auto at = [&](double y) {
        return sampler.density(column, y, terrainDensity(y, column.ground));
    };
    const double bottom = static_cast<double>(column.ground) -
                          static_cast<double>(sampler.field().params().maxDepth) - 2.0;
    double y = std::ceil(static_cast<double>(ceiling)) - 1.0;
    float previous = at(y);
    for (y -= 1.0; y >= bottom; y -= 1.0) {
        const float density = at(y);
        if (previous >= 0.0f && density < 0.0f) {
            return static_cast<float>((y + 1.0) -
                                      static_cast<double>(previous / (previous - density)));
        }
        previous = density;
    }
    return -1.0e9f;
}

struct Anchoring {
    std::size_t stalactites = 0;
    std::size_t stalagmites = 0;
    std::size_t flooded = 0;   // stalactites over a lake
    std::size_t badBase = 0;   // no rock behind the base, or no air in front
    std::size_t badTip = 0;    // tip in rock
    std::size_t badLength = 0; // out of [minLength, maxLength], or over its share of the room
    std::size_t badRadius = 0;
    std::size_t badPair = 0;    // a stalagmite not under its stalactite, or closer than the gap
    std::size_t underWater = 0; // a base or a tip under the water of its column
    std::size_t badIce = 0;
};

// Checks the speleothem s of the list at index n (its column and the void around it).
inline void checkSpeleothem(const std::vector<Speleothem>& list, std::size_t n,
                            CaveSampler& sampler, Anchoring& out) {
    const Speleothem& s = list[n];
    const SpeleothemParams& p = sampler.field().params().speleothems;
    const CaveGround& ground = sampler.ground();
    const CaveColumn column = sampler.column(s.x, s.z, ground.height(s.x, s.z));
    const auto carved = [&](double y) {
        return sampler.density(column, y, terrainDensity(y, column.ground));
    };
    const bool hanging = s.kind == SpeleothemKind::Stalactite;
    const double up = hanging ? 1.0 : -1.0; // toward the rock
    const double base = static_cast<double>(s.y);
    out.badBase += carved(base + 0.6 * up) < 0.0f && carved(base - 0.6 * up) >= 0.0f ? 0 : 1;
    out.badTip += carved(base - static_cast<double>(s.length) * up) >= 0.0f ? 0 : 1;
    out.badLength += s.length >= p.minLength - 1.0e-4f && s.length <= p.maxLength + 1.0e-4f ? 0 : 1;
    out.badRadius += std::abs(s.radius - s.length * p.radiusPerLength) <= 1.0e-4f ? 0 : 1;
    const float tip = hanging ? s.y - s.length : s.y + s.length;
    out.underWater += s.y >= column.waterLevel && tip >= column.waterLevel ? 0 : 1;
    const terrain::CaveEnvironment environment = ground.environment(s.x, s.z);
    out.badIce += s.ice == (column.frozen || environment.temperature <= p.iceTemperature) ? 0 : 1;
    const std::size_t ceilingIndex = hanging ? n : n - 1;
    if (!hanging && (n == 0 || list[n - 1].kind != SpeleothemKind::Stalactite ||
                     list[n - 1].x != s.x || list[n - 1].z != s.z)) {
        ++out.badPair;
        return;
    }
    const Speleothem& ceiling = list[ceilingIndex];
    const float floor = floorUnder(sampler, column, ceiling.y);
    const float free = ceiling.y - std::max(floor, column.waterLevel);
    if (hanging) {
        ++out.stalactites;
        out.flooded += column.waterLevel > floor ? 1 : 0;
        out.badLength +=
            ceiling.y - floor >= p.minSpan - 1.0e-3f && s.length <= p.maxShare * free + 1.0e-3f ? 0
                                                                                                : 1;
        return;
    }
    ++out.stalagmites;
    const float room = std::min(free, ceiling.y - floor - ceiling.length - 0.5f);
    out.badLength += s.length <= p.maxShare * room + 1.0e-3f ? 0 : 1;
    const float gap = (ceiling.y - ceiling.length) - (s.y + s.length);
    out.badPair +=
        std::abs(floor - s.y) <= 1.0e-3f && gap >= 0.5f - 1.0e-3f && s.ice == ceiling.ice ? 0 : 1;
}

inline Anchoring anchoring(const CaveField& field, const CaveGround& ground, const Square& s) {
    const SpeleothemField spel(kSeed, field.params().speleothems);
    CaveSampler sampler = samplerOver(field, ground, s);
    const std::vector<Speleothem> list = placeIn(spel, sampler, s);
    Anchoring result;
    for (std::size_t n = 0; n < list.size(); ++n) {
        checkSpeleothem(list, n, sampler, result);
    }
    return result;
}

// Height of the first rock under the ground of the column, going down 1 m at a time from 1 m
// under the ground: where the density crosses 0 under a sample of air (1 m under the ground)
// that opens to the sky. kDryLevel if the column does not open to the sky.
inline float skyFloor(CaveSampler& sampler, const CaveColumn& column) {
    const auto at = [&](double y) {
        return sampler.density(column, y, terrainDensity(y, column.ground));
    };
    double y = std::ceil(static_cast<double>(column.ground)) - 1.0;
    float previous = at(y);
    if (previous < 0.0f) {
        return kDryLevel;
    }
    const double bottom = static_cast<double>(column.ground) - 64.0;
    for (y -= 1.0; y >= bottom; y -= 1.0) {
        const float density = at(y);
        if (density < 0.0f) {
            return static_cast<float>((y + 1.0) -
                                      static_cast<double>(previous / (previous - density)));
        }
        previous = density;
    }
    return kDryLevel;
}

struct SkyCounts {
    std::size_t columns = 0; // columns of the squares that open to the sky
    std::size_t closed = 0;  // the others
    std::size_t inOpen = 0;  // speleothems in an open column (in a lower void)
    std::size_t inClosed = 0;
    std::size_t sharing = 0; // speleothems in the void a column opens to the sky
    std::size_t inEntranceColumn = 0; // speleothems in a column crossing an entrance
};

// Counts the dense speleothems around the top of an entrance by whether the void they stand in
// is the one their column opens to the sky.
inline void countAroundEntrance(const CaveField& field, const SpeleothemField& spel,
                                const CaveGround& ground, const CaveEntrance& e,
                                SkyCounts& counts) {
    const Square square{e.topX - 24.0, e.topZ - 24.0, 48.0};
    CaveSampler sampler = samplerOver(field, ground, square);
    for (double z = square.minZ; z < square.minZ + square.size; z += 1.0) {
        for (double x = square.minX; x < square.minX + square.size; x += 1.0) {
            const CaveColumn column = sampler.column(x, z, ground.height(x, z));
            (skyFloor(sampler, column) > kDryLevel ? counts.columns : counts.closed) += 1;
        }
    }
    for (const Speleothem& s : placeIn(spel, sampler, square)) {
        const CaveColumn column = sampler.column(s.x, s.z, ground.height(s.x, s.z));
        const float opening = skyFloor(sampler, column);
        (opening > kDryLevel ? counts.inOpen : counts.inClosed) += 1;
        counts.sharing += opening > kDryLevel && s.y >= opening - 0.01f ? 1 : 0;
        const std::vector<CaveEntrance>& near = sampler.entrances();
        const bool crossed = std::any_of(near.begin(), near.end(), [&s](const CaveEntrance& n) {
            return terrain::entranceColumn(n, s.x, s.z, 0.0);
        });
        counts.inEntranceColumn += crossed ? 1 : 0;
    }
}

inline std::vector<Speleothem> placeWith(const CaveParams& params,
                                         const terrain::CaveEnvironment& env, const Square& s) {
    const CaveField field(kSeed, params);
    const CaveGround ground = withEnvironment(flatGround(kFlatHeight), env);
    const SpeleothemField spel(kSeed, params.speleothems);
    CaveSampler sampler = samplerOver(field, ground, s);
    return placeIn(spel, sampler, s);
}

inline bool contains(const std::vector<Speleothem>& list, const Speleothem& s) {
    return std::find(list.begin(), list.end(), s) != list.end();
}

} // namespace speleothems_test
