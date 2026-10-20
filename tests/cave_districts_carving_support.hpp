#pragma once

// Helpers of the tests of the cave districts (Phase 8C-1) that carve, flood or stream them:
// caves whose districts all behave as one profile (to measure the effect of a profile apart from
// the random draw of the types), small districts so that a small area spans several of them,
// the districts and the borders of a field found from the field itself, and the scan of a
// window with a chosen detail.

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"

#include "caves_test_support.hpp"

#include <cstddef>
#include <vector>

namespace districts_test {

using terrain::CaveDetail;
using terrain::CaveDistrict;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;

constexpr std::size_t index(CaveDistrictType type) {
    return static_cast<std::size_t>(type);
}

inline CaveProfile defaultProfile(CaveDistrictType type) {
    return terrain::defaultDistrictProfiles()[index(type)];
}

// Caves with the cave regions of the default parameters (3 km districts blended over 280 m).
inline CaveParams defaultCaves() {
    CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    return params;
}

// Caves whose districts all behave as profile, whatever type each draws.
inline CaveParams uniformCaves(const CaveProfile& profile) {
    CaveParams params = defaultCaves();
    params.districts->profiles.fill(profile);
    return params;
}

// Districts of about half a kilometer, blended over 90 m (the widest blend of this cell size is
// 0.189 of it): a window of a few hundred meters spans several districts and their borders.
inline CaveParams denseCaves() {
    CaveParams params = defaultCaves();
    params.districts->cellSize = 480.0;
    params.districts->blend = 90.0;
    return params;
}

// The same caves with every aquifer dry: only the shape of the caves is left.
inline CaveParams dry(CaveParams params) {
    params.aquifers.dryChance = 0.0f;
    params.aquifers.wetChance = 0.0f;
    return params;
}

// Detail of the caves without the barriers and the water.
inline CaveDetail withoutAquifers() {
    CaveDetail detail;
    detail.components =
        terrain::kAllCaveComponents & ~static_cast<unsigned>(terrain::kCaveAquifers);
    return detail;
}

// Cells [first, first + count) of the grid of the districts, on both axes.
struct Cells {
    int first;
    int count;
};

// The districts of the type whose site lies on ground at least minHeight high.
inline std::vector<CaveDistrict> districtsOf(const CaveField& field, const CaveGround& ground,
                                             const Cells& cells, CaveDistrictType type,
                                             float minHeight) {
    std::vector<CaveDistrict> found;
    for (int cellZ = cells.first; cellZ < cells.first + cells.count; ++cellZ) {
        for (int cellX = cells.first; cellX < cells.first + cells.count; ++cellX) {
            const CaveDistrict district = field.district(cellX, cellZ, ground);
            if (district.type == type &&
                ground.height(district.siteX, district.siteZ) >= minHeight) {
                found.push_back(district);
            }
        }
    }
    return found;
}

// A border between two neighbor districts of different types, at the midpoint of their sites
// (where only the two weigh, half each).
struct Border {
    CaveDistrict a;
    CaveDistrict b;
    double x = 0.0;
    double z = 0.0;
};

// Borders of the cells, each district with its east and south neighbors, over ground in
// [minHeight, maxHeight] at the midpoint.
inline std::vector<Border> bordersOf(const CaveField& field, const CaveGround& ground,
                                     const Cells& cells, float minHeight, float maxHeight) {
    terrain::AquiferCache cache(field, ground);
    std::vector<Border> found;
    for (int cellZ = cells.first; cellZ < cells.first + cells.count; ++cellZ) {
        for (int cellX = cells.first; cellX < cells.first + cells.count; ++cellX) {
            const CaveDistrict a = field.district(cellX, cellZ, ground);
            for (const CaveDistrict& b : {field.district(cellX + 1, cellZ, ground),
                                          field.district(cellX, cellZ + 1, ground)}) {
                const double x = (a.siteX + b.siteX) / 2.0;
                const double z = (a.siteZ + b.siteZ) / 2.0;
                const float height = ground.height(x, z);
                if (a.type == b.type || height < minHeight || height > maxHeight ||
                    cache.districtWeights(x, z).count != 2) {
                    continue;
                }
                found.push_back({a, b, x, z});
            }
        }
    }
    return found;
}

// Calls visit(sampler, column, y, rock) for the samples of the window, from one sampler of the
// detail over the whole window: the columns every w.step meters, and in each of them the depths
// from w.minDepth to w.maxDepth below its ground every depthStep meters (rock: the terrain
// density there).
template <typename Visit>
void forEachSample(const CaveField& field, const CaveGround& ground, const CaveDetail& detail,
                   const caves_test::Window& w, int depthStep, Visit visit) {
    struct Column {
        double x;
        double z;
        float ground;
    };
    std::vector<Column> columns;
    caves_test::BoxBuilder builder;
    const int countX = static_cast<int>(w.sizeX / w.step) + 1;
    const int countZ = static_cast<int>(w.sizeZ / w.step) + 1;
    for (int j = 0; j < countZ; ++j) {
        for (int i = 0; i < countX; ++i) {
            const double x = w.minX + i * w.step;
            const double z = w.minZ + j * w.step;
            const float height = ground.height(x, z);
            columns.push_back({x, z, height});
            builder.add(x, static_cast<double>(height) - w.maxDepth, z);
            builder.add(x, static_cast<double>(height) - w.minDepth, z);
        }
    }
    CaveSampler sampler(field, ground, detail, builder.box(1.0));
    for (const Column& c : columns) {
        const terrain::CaveColumn column = sampler.column(c.x, c.z, c.ground);
        for (int depth = w.minDepth; depth <= w.maxDepth; depth += depthStep) {
            const double y = static_cast<double>(c.ground) - depth;
            visit(sampler, column, y, caves_test::terrainDensity(y, c.ground));
        }
    }
}

} // namespace districts_test
