// Materials of the cave regions (cave_materials.hpp): basalt in volcanic regions and moss down to
// lushDepth in lush regions, in the priority of the rules (the outer surface, then ice, mud,
// basalt, moss, then the rules of the rock), no change at all where no region applies (against the
// rules as they were before the regions), and the walls of the streamed tiles painted from the
// region they lie in.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_environment_test_support.hpp"
#include "cave_materials_test_support.hpp"
#include "caves_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using cave_materials_test::stoneWall;
using terrain::Biome;
using terrain::CaveMaterial;
using terrain::CaveMaterialParams;
using terrain::CaveWallSite;

constexpr int kBiomes = terrain::kBiomeCount;

CaveWallSite regionWall(bool icy, bool lush, bool volcanic) {
    CaveWallSite site = stoneWall();
    site.icyRegion = icy;
    site.lushRegion = lush;
    site.volcanicRegion = volcanic;
    return site;
}

// The rules as they were before the cave regions (Phase 8B).
CaveMaterial rulesWithoutRegions(const CaveWallSite& s, const CaveMaterialParams& p) {
    const terrain::CaveEnvironment& e = s.environment;
    const bool wet = s.aboveWater <= p.wetHeight;
    const bool dry =
        e.biome == Biome::Desert || e.biome == Biome::Savanna || e.biome == Biome::Beach;
    const bool mossy = e.biome == Biome::TemperateForest || e.biome == Biome::TropicalForest ||
                       e.biome == Biome::Marsh || e.biome == Biome::Taiga;
    if (s.depth < p.surfaceDepth) {
        return CaveMaterial::Surface;
    }
    if ((wet && s.frozenWater) || (e.temperature <= p.iceTemperature && s.depth <= p.iceDepth)) {
        return CaveMaterial::Ice;
    }
    if (wet) {
        return CaveMaterial::Mud;
    }
    if (dry) {
        return CaveMaterial::Sandstone;
    }
    if (s.depth <= p.mossDepth && e.humidity >= p.mossHumidity && mossy) {
        return CaveMaterial::Moss;
    }
    if (s.normalY >= p.floorNormal) {
        return CaveMaterial::Gravel;
    }
    if (s.depth >= p.deepDepth) {
        return CaveMaterial::Deepstone;
    }
    return e.humidity >= p.limestoneHumidity ? CaveMaterial::Limestone : CaveMaterial::Stone;
}

// Calls visit(site) for a sweep of walls: every biome, cold to hot, dry to humid, from the surface
// to the deep, floors, walls and ceilings, away from a lake, in it and next to it (frozen or not).
template <typename Visit> void forEachSite(Visit visit) {
    struct Lake {
        float aboveWater;
        bool frozen;
    };
    constexpr std::array<Lake, 6> kLakes = {{{1.0e9f, false},
                                             {0.0f, false},
                                             {0.0f, true},
                                             {3.0f, true},
                                             {3.5f, false},
                                             {-5.0f, false}}};
    for (int biome = 0; biome < kBiomes; ++biome) {
        for (const float temperature : {-12.0f, -2.0f, 4.0f, 18.0f}) {
            for (const float humidity : {0.05f, 0.45f, 0.62f, 0.95f}) {
                for (const float depth :
                     {0.5f, 1.5f, 10.0f, 20.0f, 25.0f, 31.0f, 40.0f, 41.0f, 70.0f, 80.0f, 200.0f}) {
                    for (const float normalY : {-0.9f, 0.0f, 0.7f, 0.8f}) {
                        for (const Lake& lake : kLakes) {
                            CaveWallSite site;
                            site.environment = {static_cast<Biome>(biome), temperature, humidity};
                            site.depth = depth;
                            site.normalY = normalY;
                            site.aboveWater = lake.aboveWater;
                            site.frozenWater = lake.frozen;
                            visit(site);
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------

// Basalt in a volcanic region, from the surface depth on, whatever the wall, the climate and the
// depth, before sandstone, moss, gravel, deepstone and limestone; the outer surface and the cold
// shallow caves (ice) still come first.
void testBasalt() {
    const CaveMaterialParams p;
    int walls = 0;
    int basalt = 0;
    for (int biome = 0; biome < kBiomes; ++biome) {
        for (const float humidity : {0.1f, 0.9f}) {
            for (const float depth : {p.surfaceDepth, 10.0f, 25.0f, 50.0f, 80.0f, 300.0f}) {
                for (const float normalY : {-0.9f, 0.0f, 0.8f}) {
                    CaveWallSite site = regionWall(false, false, true);
                    site.environment = {static_cast<Biome>(biome), 12.0f, humidity};
                    site.depth = depth;
                    site.normalY = normalY;
                    ++walls;
                    basalt += caveMaterial(site, p) == CaveMaterial::Basalt ? 1 : 0;
                }
            }
        }
    }
    TEST_CHECK(basalt == walls);

    CaveWallSite site = regionWall(false, false, true);
    site.depth = std::nextafter(p.surfaceDepth, 0.0f);
    TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Surface);
    site.depth = 10.0f;
    TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Basalt);
    TEST_CHECK(caveMaterial(stoneWall(), p) == CaveMaterial::Stone); // the same wall without it

    // Cold and shallow: ice; colder or not, deeper than iceDepth: basalt.
    site.environment.temperature = p.iceTemperature;
    site.depth = p.iceDepth;
    TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Ice);
    site.depth = std::nextafter(p.iceDepth, 1.0e9f);
    TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Basalt);
    site.depth = 10.0f;
    site.environment.temperature = std::nextafter(p.iceTemperature, 100.0f);
    TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Basalt);
}

// Ice beats the regions: an icy region is ice at every depth below the surface (with a volcanic or
// lush region too, and next to a lake that is not frozen), and a frozen lake shore is ice in them.
void testIceBeatsBasaltAndMoss() {
    const CaveMaterialParams p;
    for (const float depth : {p.surfaceDepth, 5.0f, 20.0f, 40.0f, 60.0f, 200.0f}) {
        for (const bool lush : {false, true}) {
            for (const bool volcanic : {false, true}) {
                CaveWallSite site = regionWall(true, lush, volcanic);
                site.depth = depth;
                TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Ice);
                site.aboveWater = 0.0f; // next to a lake that is not frozen
                TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Ice);
            }
        }
    }
    CaveWallSite icy = regionWall(true, true, true);
    icy.depth = std::nextafter(p.surfaceDepth, 0.0f);
    TEST_CHECK(caveMaterial(icy, p) == CaveMaterial::Surface);

    CaveWallSite shore = regionWall(false, true, true);
    shore.aboveWater = p.wetHeight;
    shore.frozenWater = true;
    TEST_CHECK(caveMaterial(shore, p) == CaveMaterial::Ice);
}

// Mud next to a lake that is not frozen (at most wetHeight above its level, or under it) beats
// the regions; a little higher, the regions apply again.
void testMudBeatsBasaltAndMoss() {
    const CaveMaterialParams p;
    for (const bool lush : {false, true}) {
        for (const bool volcanic : {false, true}) {
            CaveWallSite site = regionWall(false, lush, volcanic);
            const CaveMaterial dry = !volcanic && !lush
                                         ? CaveMaterial::Stone
                                         : (volcanic ? CaveMaterial::Basalt : CaveMaterial::Moss);
            for (const float aboveWater : {-5.0f, 0.0f, p.wetHeight}) {
                site.aboveWater = aboveWater;
                TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Mud);
            }
            site.aboveWater = std::nextafter(p.wetHeight, 1.0e9f);
            TEST_CHECK(caveMaterial(site, p) == dry);
        }
    }
}

// Basalt beats moss where both regions apply.
void testBasaltBeatsMoss() {
    const CaveMaterialParams p;
    CaveWallSite site = regionWall(false, true, true);
    for (const float depth : {2.0f, 10.0f, 30.0f, 40.0f, 80.0f}) {
        site.depth = depth;
        TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Basalt);
    }
}

// Moss in a lush region down to lushDepth included, whatever the biome and the climate, before
// sandstone, gravel, deepstone and limestone; below it the wall is the one without the region.
void testMossDepth() {
    const CaveMaterialParams p;
    TEST_CHECK(p.lushDepth == 40.0f && p.lushDepth > p.mossDepth);
    for (int biome = 0; biome < kBiomes; ++biome) {
        for (const float normalY : {-0.9f, 0.8f}) {
            for (const float depth : {p.surfaceDepth, 10.0f, p.mossDepth, 30.0f, p.lushDepth}) {
                CaveWallSite site = regionWall(false, true, false);
                site.environment = {static_cast<Biome>(biome), 30.0f, 0.0f}; // hot and dry
                site.depth = depth;
                site.normalY = normalY;
                TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Moss);
            }
            for (const float depth : {std::nextafter(p.lushDepth, 1.0e9f), 41.0f, 80.0f, 300.0f}) {
                CaveWallSite site = regionWall(false, true, false);
                site.environment = {static_cast<Biome>(biome), 30.0f, 0.0f};
                site.depth = depth;
                site.normalY = normalY;
                CaveWallSite plain = site;
                plain.lushRegion = false;
                TEST_CHECK(caveMaterial(site, p) == caveMaterial(plain, p));
                TEST_CHECK(caveMaterial(site, p) != CaveMaterial::Moss);
            }
        }
    }
    CaveWallSite site = regionWall(false, true, false);
    site.depth = std::nextafter(p.surfaceDepth, 0.0f);
    TEST_CHECK(caveMaterial(site, p) == CaveMaterial::Surface);

    CaveMaterialParams shallow;
    shallow.lushDepth = 25.0f;
    site.depth = 25.0f;
    TEST_CHECK(caveMaterial(site, shallow) == CaveMaterial::Moss);
    site.depth = std::nextafter(25.0f, 100.0f);
    TEST_CHECK(caveMaterial(site, shallow) == CaveMaterial::Stone);
    CaveMaterialParams deep;
    deep.lushDepth = 100.0f;
    site.depth = 90.0f;
    TEST_CHECK(caveMaterial(site, deep) == CaveMaterial::Moss);
}

// Without any region the materials are exactly the ones of the rules before them, over a sweep of
// 50,688 walls; with regions, the surface, ice and mud are never changed by basalt and moss
// (an icy region makes ice of everything below the surface), and basalt and moss apply to what
// remains, in that order.
void testRegionsOffAndOn() {
    const CaveMaterialParams p;
    std::size_t walls = 0;
    std::size_t same = 0;
    std::size_t consistent = 0;
    forEachSite([&](const CaveWallSite& site) {
        const CaveMaterial before = rulesWithoutRegions(site, p);
        ++walls;
        same += caveMaterial(site, p) == before ? 1 : 0;
        bool ok = true;
        for (const bool icy : {false, true}) {
            for (const bool lush : {false, true}) {
                for (const bool volcanic : {false, true}) {
                    CaveWallSite regional = site;
                    regional.icyRegion = icy;
                    regional.lushRegion = lush;
                    regional.volcanicRegion = volcanic;
                    CaveMaterial expected = before;
                    const bool fixed = before == CaveMaterial::Surface ||
                                       before == CaveMaterial::Ice || before == CaveMaterial::Mud;
                    if (icy && before != CaveMaterial::Surface) {
                        expected = CaveMaterial::Ice;
                    } else if (!fixed && volcanic) {
                        expected = CaveMaterial::Basalt;
                    } else if (!fixed && lush && site.depth <= p.lushDepth) {
                        expected = CaveMaterial::Moss;
                    }
                    ok = ok && caveMaterial(regional, p) == expected;
                }
            }
        }
        consistent += ok ? 1 : 0;
    });
    std::printf("sweep: %zu walls, %zu as before without regions, %zu consistent with them\n",
                walls, same, consistent);
    TEST_CHECK(walls == 12 * 4 * 4 * 11 * 4 * 6);
    TEST_CHECK(same == walls);
    TEST_CHECK(consistent == walls);
}

// The names of basalt and magma, the materials added after the ones of 8B, in that order.
void testBasaltName() {
    TEST_CHECK(std::string(terrain::caveMaterialName(CaveMaterial::Basalt)) == "basalt");
    TEST_CHECK(std::string(terrain::caveMaterialName(CaveMaterial::Magma)) == "magma");
    TEST_CHECK(terrain::kCaveMaterialCount == 12);
    TEST_CHECK(static_cast<int>(CaveMaterial::Basalt) == terrain::kCaveMaterialCount - 2);
    TEST_CHECK(static_cast<int>(CaveMaterial::Magma) == terrain::kCaveMaterialCount - 1);
}

// ---------------------------------------------------------------------------

// Wall vertices at least surfaceDepth under the ground, by material.
struct WallCounts {
    std::size_t deep = 0;
    std::array<std::size_t, terrain::kCaveMaterialCount> byMaterial{};
    std::size_t shallow = 0;      // of them, at most lushDepth under the ground
    std::size_t shallowOther = 0; // of those, neither moss nor mud
    std::size_t deeper = 0;       // the others
    std::size_t mossDeeper = 0;   // of those, moss

    [[nodiscard]] std::size_t of(CaveMaterial material) const {
        return byMaterial[static_cast<std::size_t>(material)];
    }
};

// The walls of 5 x 5 tiles of LOD 0, spread over 160 m, around (x, z).
WallCounts paintedWalls(const terrain::TerrainTiles& tiles, double x, double z) {
    const CaveMaterialParams params = tiles.caves()->params().materials;
    const terrain::ChunkCoord center = terrain::worldToChunk(x, z);
    WallCounts counts;
    for (int dz = -4; dz <= 4; dz += 2) {
        for (int dx = -4; dx <= 4; dx += 2) {
            const terrain::ChunkCoord chunk{center.x + dx, center.z + dz};
            const terrain::TileData data = tiles.generate({terrain::tileContaining(chunk, 0), 0});
            for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
                const streaming_test::WorldPoint p = streaming_test::worldVertex(data, n);
                const double depth =
                    static_cast<double>(tiles.heightfield().heightAt(p.x, p.z)) - p.y;
                if (depth < static_cast<double>(params.surfaceDepth)) {
                    continue;
                }
                const CaveMaterial material = data.materials[n];
                ++counts.deep;
                ++counts.byMaterial[static_cast<std::size_t>(material)];
                const bool moss = material == CaveMaterial::Moss;
                if (depth > static_cast<double>(params.lushDepth)) {
                    ++counts.deeper;
                    counts.mossDeeper += moss ? 1 : 0;
                } else {
                    ++counts.shallow;
                    counts.shallowOther += moss || material == CaveMaterial::Mud ? 0 : 1;
                }
            }
        }
    }
    return counts;
}

// The walls of the tiles take the material of the region they lie in (districts of the default
// world with seed 1337, in a single district): basalt in a volcanic one (magma next to its lava),
// ice in a glacier, moss down to lushDepth in a lush one (with mud next to its lakes), and nothing
// of the other regions.
void testRegionsPaintTheWalls() {
    terrain::CaveParams caves;
    caves.districts = terrain::CaveDistrictParams{};
    const terrain::TerrainTiles tiles(1337, {}, {}, caves);

    const WallCounts volcanic = paintedWalls(tiles, 13568.0, 7012.0);
    std::printf("volcanic: %zu wall vertices, %zu basalt, %zu magma\n", volcanic.deep,
                volcanic.of(CaveMaterial::Basalt), volcanic.of(CaveMaterial::Magma));
    TEST_CHECK(volcanic.deep > 5000);
    TEST_CHECK((volcanic.of(CaveMaterial::Basalt) + volcanic.of(CaveMaterial::Magma)) * 100 >=
               volcanic.deep * 99);
    TEST_CHECK(volcanic.of(CaveMaterial::Moss) == 0 && volcanic.of(CaveMaterial::Ice) == 0);

    const WallCounts glacier = paintedWalls(tiles, 17023.0, 22087.0);
    std::printf("glacier: %zu wall vertices, %zu ice\n", glacier.deep,
                glacier.of(CaveMaterial::Ice));
    TEST_CHECK(glacier.deep > 5000);
    TEST_CHECK(glacier.of(CaveMaterial::Ice) * 100 >= glacier.deep * 99);
    TEST_CHECK(glacier.of(CaveMaterial::Moss) == 0 && glacier.of(CaveMaterial::Basalt) == 0);

    const WallCounts lush = paintedWalls(tiles, -19282.0, -1731.0);
    std::printf("lush: %zu wall vertices, %zu moss, %zu mud; %zu down to lushDepth (%zu neither), "
                "%zu deeper (%zu moss)\n",
                lush.deep, lush.of(CaveMaterial::Moss), lush.of(CaveMaterial::Mud), lush.shallow,
                lush.shallowOther, lush.deeper, lush.mossDeeper);
    TEST_CHECK(lush.shallow > 2000 && lush.deeper > 500);
    TEST_CHECK(lush.of(CaveMaterial::Moss) > 1000 && lush.of(CaveMaterial::Mud) > 100);
    TEST_CHECK(lush.shallowOther == 0); // down to lushDepth, moss or mud beside a lake
    TEST_CHECK(lush.mossDeeper == 0);
    TEST_CHECK(lush.of(CaveMaterial::Basalt) == 0 && lush.of(CaveMaterial::Ice) == 0);
}

} // namespace

int main() {
    testBasalt();
    testIceBeatsBasaltAndMoss();
    testMudBeatsBasaltAndMoss();
    testBasaltBeatsMoss();
    testMossDepth();
    testRegionsOffAndOn();
    testBasaltName();
    testRegionsPaintTheWalls();
    return test::result();
}
