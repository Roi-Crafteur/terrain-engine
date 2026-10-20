// Objects and materials around the rivers and the lava (Phase 8C-2): no stalagmite stands under
// the water of a river and every tip hangs above it; no speleothem and no plant lies in a lava
// cavern; the walls of a gallery next to its river turn to mud, and the water of a river is ice
// where its cell is cold, as the lakes.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_flora.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_environment_test_support.hpp"
#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using caves_test::kSeed;
using terrain::AquiferCache;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::Speleothem;
using terrain::SpeleothemKind;

constexpr float kFlatHeight = 300.4f;

// Over a world of Rivers regions on flat ground: the speleothems over the galleries never stand
// under the water of their river, and their tips hang above it.
void testSpeleothemsOverRivers() {
    const terrain::CaveGround ground = caves_test::withEnvironment(
        caves_test::flatGround(kFlatHeight), caves_test::humidEnvironment());
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const terrain::SpeleothemField speleothems(kSeed, field.params().speleothems);
    int overRivers = 0;
    int underWater = 0;
    for (int n = 0; n < 6; ++n) {
        const double x0 = 1000.0 + 256.0 * n;
        const double z0 = -2000.0;
        CaveSampler sampler(field, ground, terrain::CaveDetail{},
                            {x0, 200.0, z0, x0 + 256.0, 310.0, z0 + 256.0});
        for (const Speleothem& s : speleothems.place(sampler, x0, z0, x0 + 256.0, z0 + 256.0)) {
            const CaveColumn c = sampler.column(s.x, s.z, kFlatHeight);
            const float base = s.kind == SpeleothemKind::Stalagmite ? s.y : s.y - s.length;
            const float water = field.waterLevelAt(c, static_cast<double>(base));
            if (c.riverLevel <= terrain::kDryLevel || water != c.riverLevel) {
                continue;
            }
            ++overRivers;
            underWater += base <= water ? 1 : 0;
        }
    }
    std::printf("speleothems: %d over the water of a river, %d reaching into it\n", overRivers,
                underWater);
    TEST_CHECK(overRivers > 0);
    TEST_CHECK(underWater == 0);
}

// In a world of volcanic and lush regions on the relief: speleothems and plants, but none of
// them in the columns of a lava cell.
void testNothingInLava() {
    CaveParams params = galleries_test::uniformParams(CaveDistrictType::Volcanic);
    for (CaveProfile& profile : params.districts->profiles) {
        profile[CaveProfile::kLush] = 1.0f;
        profile[CaveProfile::kVolcanic] = 0.0f;
    }
    const terrain::Heightfield heightfield(kSeed);
    const CaveGround ground = caves_test::withEnvironment(
        caves_test::heightfieldGround(heightfield), caves_test::humidEnvironment());
    const CaveField field(kSeed, params);
    const terrain::SpeleothemField speleothems(kSeed, params.speleothems);
    const terrain::CaveFloraField flora(kSeed, params.districts->flora);
    AquiferCache cache(field, ground);
    std::size_t objects = 0;
    std::size_t inLava = 0;
    std::size_t lavaColumns = 0;
    for (int n = 0; n < 9; ++n) {
        const double x0 = -600.0 + 400.0 * (n % 3);
        const double z0 = 600.0 + 400.0 * (n / 3);
        CaveSampler sampler(field, ground, terrain::CaveDetail{},
                            {x0, -600.0, z0, x0 + 128.0, 2400.0, z0 + 128.0});
        const auto check = [&](double x, double z) {
            ++objects;
            inLava += cache.nearest(x, z).lava ? 1 : 0;
        };
        for (const Speleothem& s : speleothems.place(sampler, x0, z0, x0 + 128.0, z0 + 128.0)) {
            check(s.x, s.z);
        }
        for (const terrain::CaveFlora& f : flora.place(sampler, x0, z0, x0 + 128.0, z0 + 128.0)) {
            check(f.x, f.z);
        }
        for (double z = z0; z < z0 + 128.0; z += 8.0) {
            for (double x = x0; x < x0 + 128.0; x += 8.0) {
                lavaColumns += cache.nearest(x, z).lava ? 1 : 0;
            }
        }
    }
    std::printf("objects: %zu speleothems and plants, %zu in lava cells (%zu of 2304 columns in "
                "lava cells)\n",
                objects, inLava, lavaColumns);
    TEST_CHECK(objects > 50 && lavaColumns > 200);
    TEST_CHECK(inLava == 0);
}

// The tiles of a world of Rivers regions on the heightfield over rivers: mud on the walls next to
// the water, and every water vertex ice exactly where its cell freezes it (a frozen lake, or a
// cold cell under a river), water elsewhere.
void testRiverMaterials() {
    const terrain::TerrainTiles tiles(kSeed, {}, {},
                                      galleries_test::uniformParams(CaveDistrictType::Rivers));
    const CaveField& field = *tiles.caves();
    AquiferCache cache(field, tiles.caveGround());
    std::size_t riverTiles = 0;
    std::size_t mud = 0;
    std::size_t waterVertices = 0;
    std::size_t inconsistent = 0;
    for (int n = 0; n < 400 && riverTiles < 4; ++n) {
        const terrain::ChunkCoord chunk{n % 20 * 7 - 60, n / 20 * 7 - 60};
        const terrain::TileData data = tiles.generate({{chunk, 0}, 0});
        if (data.water.vertices.empty()) {
            continue;
        }
        ++riverTiles;
        mud += static_cast<std::size_t>(
            std::count(data.materials.begin(), data.materials.end(), terrain::CaveMaterial::Mud));
        for (std::size_t v = 0; v < data.water.vertices.size(); ++v) {
            const double x = data.originX + data.water.vertices[v].x;
            const double z = data.originZ + data.water.vertices[v].z;
            const terrain::AquiferCell& cell = cache.nearest(x, z);
            const bool river = cache.profileAt(x, z)[CaveProfile::kRiver] >=
                               field.params().rivers.threshold;
            const bool ice = cell.frozen || (cell.cold && river);
            ++waterVertices;
            inconsistent += (data.waterMaterials[v] == terrain::CaveMaterial::Ice) != ice ? 1 : 0;
        }
    }
    std::printf("river tiles: %zu, %zu mud wall vertices, %zu water vertices (%zu with the wrong "
                "ice)\n",
                riverTiles, mud, waterVertices, inconsistent);
    TEST_CHECK(riverTiles == 4 && mud > 0 && waterVertices > 0);
    TEST_CHECK(inconsistent == 0);
}

} // namespace

int main() {
    testSpeleothemsOverRivers();
    testNothingInLava();
    testRiverMaterials();
    return test::result();
}
