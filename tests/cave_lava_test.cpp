// Lava caverns of the volcanic cave regions (Phase 8C-2): a cell that is not wet may hold lava at
// the bottom of its deep caverns. The wet cells and their lakes are those of 8C-1; the lava stays
// under the band of the rivers of every gallery of its cell, in the carved air under its level
// and under the ground, flat, never against dry air nor next to water; the entrances stay above
// it; magma lines the walls just above it; the tiles of LOD 0 and 1 mesh it, LOD 2 none.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_districts_carving_support.hpp"
#include "cave_environment_test_support.hpp"
#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using caves_test::kSeed;
using terrain::AquiferCache;
using terrain::AquiferCell;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;

CaveParams volcanic(float lava) {
    CaveParams params = galleries_test::uniformParams(CaveDistrictType::Volcanic);
    for (CaveProfile& profile : params.districts->profiles) {
        profile[CaveProfile::kLava] = lava;
    }
    return params;
}

// With lava or without it, the cells keep their sites, wetness, levels and ice; lava only fills
// cells that are not wet, about lavaChance of them.
void testCellsUnchanged() {
    const terrain::Heightfield heightfield(kSeed, test_relief::legacyRelief());
    const CaveGround ground = caves_test::withEnvironment(
        caves_test::heightfieldGround(heightfield), caves_test::humidEnvironment());
    const CaveField withLava(kSeed, volcanic(1.0f));
    const CaveField without(kSeed, volcanic(0.0f));
    int same = 0;
    int cells = 0;
    int dry = 0;
    int lava = 0;
    int lavaInWet = 0;
    for (int z = -20; z < 20; ++z) {
        for (int x = -20; x < 20; ++x) {
            const AquiferCell a = withLava.aquifer(x, z, ground);
            const AquiferCell b = without.aquifer(x, z, ground);
            ++cells;
            same += a.wet == b.wet && a.level == b.level && a.frozen == b.frozen &&
                            a.siteX == b.siteX && a.siteZ == b.siteZ && !b.lava
                        ? 1
                        : 0;
            dry += a.wet ? 0 : 1;
            lava += a.lava ? 1 : 0;
            lavaInWet += a.lava && a.wet ? 1 : 0;
        }
    }
    const double share = dry == 0 ? 0.0 : static_cast<double>(lava) / dry;
    std::printf("cells: %d, %d unchanged, %d dry, %d with lava (%.2f of the dry ones)\n", cells,
                same, dry, lava, share);
    TEST_CHECK(same == cells);
    TEST_CHECK(lavaInWet == 0);
    TEST_CHECK(dry > 300 && std::abs(share - 0.6) < 0.06);
}

// Lava cells of a volcanic world on the relief, and a point of each where the lava shows: carved
// air under its level, found on 4 m columns from the level down 10 m.
struct LavaSite {
    AquiferCell cell;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool visible = false;
};

LavaSite inspectLava(const CaveField& field, const CaveGround& ground, const AquiferCell& cell) {
    LavaSite site{cell};
    const double size = field.params().aquifers.cellSize;
    const double level = static_cast<double>(cell.lavaLevel);
    CaveSampler sampler(field, ground, terrain::CaveDetail{},
                        {cell.siteX - size, level - 12.0, cell.siteZ - size, cell.siteX + size,
                         level + 1.0, cell.siteZ + size});
    for (double z = cell.siteZ - 96.0; z <= cell.siteZ + 96.0 && !site.visible; z += 4.0) {
        for (double x = cell.siteX - 96.0; x <= cell.siteX + 96.0 && !site.visible; x += 4.0) {
            const CaveColumn c = sampler.column(x, z, ground.height(x, z));
            if (c.lavaLevel != cell.lavaLevel) {
                continue;
            }
            for (double depth = 0.5; depth <= 10.0; depth += 1.0) {
                const double y = static_cast<double>(c.lavaLevel) - depth;
                const float rock = caves_test::terrainDensity(y, c.ground);
                if (sampler.lavaDensity(c, y, rock, sampler.density(c, y, rock)) < 0.0f) {
                    site = {cell, x, y, z, true};
                    break;
                }
            }
        }
    }
    return site;
}

std::vector<LavaSite> lavaSites(const CaveField& field, const CaveGround& ground, int count) {
    std::vector<LavaSite> sites;
    for (int n = 0; n < 1600 && static_cast<int>(sites.size()) < count; ++n) {
        const AquiferCell cell = field.aquifer(n % 40 - 20, n / 40 - 20, ground);
        if (cell.lava && ground.height(cell.siteX, cell.siteZ) > 20.0f) {
            sites.push_back(inspectLava(field, ground, cell));
        }
    }
    return sites;
}

// The lava stays 5 m at least under the floor of every gallery of its cell (shell and
// clearance), read from the columns of the cell every 4 m: the bands of the rivers never meet it.
void testUnderGalleries() {
    const terrain::Heightfield heightfield(kSeed, test_relief::legacyRelief());
    const CaveGround ground = caves_test::heightfieldGround(heightfield);
    const CaveField field(kSeed, volcanic(1.0f));
    const CaveParams& p = field.params();
    const double gap = static_cast<double>(p.rivers.shell + p.aquifers.lavaClearance);
    AquiferCache cache(field, ground);
    int cells = 0;
    std::size_t columns = 0;
    double closest = 1.0e9;
    for (int n = 0; n < 400 && cells < 12; ++n) {
        const AquiferCell cell = field.aquifer(n % 20 - 10, n / 20 - 10, ground);
        if (!cell.lava) {
            continue;
        }
        ++cells;
        for (double z = cell.siteZ - 200.0; z <= cell.siteZ + 200.0; z += 4.0) {
            for (double x = cell.siteX - 200.0; x <= cell.siteX + 200.0; x += 4.0) {
                const CaveColumn c = field.column(x, z, ground.height(x, z), ground, {}, &cache);
                if (c.lavaLevel != cell.lavaLevel || cache.nearest(x, z).cellX != cell.cellX ||
                    cache.nearest(x, z).cellZ != cell.cellZ) {
                    continue;
                }
                ++columns;
                closest = std::min(closest, static_cast<double>(c.galleryFloor) -
                                                static_cast<double>(c.lavaLevel));
            }
        }
    }
    std::printf("under the galleries: %d lava cells, %zu columns, the lava %.2f m under the "
                "floor at the closest\n",
                cells, columns, closest);
    TEST_CHECK(cells == 12 && columns > 5000);
    TEST_CHECK(closest >= gap);
}

struct LavaCounts {
    std::size_t lava = 0;
    std::size_t inRock = 0;      // within the skin of the rock
    std::size_t outsideAir = 0;  // deeper in the rock than the skin
    std::size_t aboveLevel = 0;  // at or over the level of the lava of its column
    std::size_t aboveGround = 0; // within groundMargin of the ground, or over it
    std::size_t dryAir = 0;      // cave air under the level and the ground without lava
    std::size_t walls = 0;       // lava with a horizontal neighbor of air without lava
    std::size_t nearWater = 0;   // lava with a neighbor of water
};

// The densities of a lattice of 1 m around a point: x varies fastest, then y, then z.
struct FluidLattice {
    static constexpr int kHalf = 20;
    static constexpr int kSide = 2 * kHalf + 1;
    static constexpr int kHeight = 14;
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    std::vector<CaveColumn> columns; // x varies fastest
    std::vector<float> rock;
    std::vector<float> carved;
    std::vector<float> lava;
    std::vector<float> water;

    [[nodiscard]] static std::size_t at(int i, int j, int k) {
        return static_cast<std::size_t>((k * kHeight + j) * kSide + i);
    }
    [[nodiscard]] const CaveColumn& column(int i, int k) const {
        return columns[static_cast<std::size_t>(k * kSide + i)];
    }
};

FluidLattice readLattice(const CaveField& field, const CaveGround& ground, const LavaSite& site) {
    using L = FluidLattice;
    FluidLattice l;
    l.x0 = std::floor(site.x) - L::kHalf;
    l.z0 = std::floor(site.z) - L::kHalf;
    l.y0 = std::floor(site.y) - 8.0;
    CaveSampler sampler(field, ground, terrain::CaveDetail{},
                        {l.x0, l.y0, l.z0, l.x0 + L::kSide, l.y0 + L::kHeight, l.z0 + L::kSide});
    for (int k = 0; k < L::kSide; ++k) {
        for (int i = 0; i < L::kSide; ++i) {
            l.columns.push_back(sampler.column(l.x0 + i, l.z0 + k, ground.height(l.x0 + i,
                                                                                 l.z0 + k)));
        }
    }
    const auto count = static_cast<std::size_t>(L::kSide * L::kSide * L::kHeight);
    l.rock.resize(count);
    l.carved.resize(count);
    l.lava.resize(count);
    l.water.resize(count);
    for (int k = 0; k < L::kSide; ++k) {
        for (int i = 0; i < L::kSide; ++i) {
            const CaveColumn& c = l.column(i, k);
            for (int j = 0; j < L::kHeight; ++j) {
                const double y = l.y0 + j;
                const std::size_t n = L::at(i, j, k);
                l.rock[n] = caves_test::terrainDensity(y, c.ground);
                l.carved[n] = sampler.density(c, y, l.rock[n]);
                l.lava[n] = sampler.lavaDensity(c, y, l.rock[n], l.carved[n]);
                l.water[n] = sampler.waterDensity(c, y, l.rock[n], l.carved[n]);
            }
        }
    }
    return l;
}

// Properties of the lava of the inner samples of a lattice.
void countLava(const FluidLattice& l, const terrain::AquiferParams& a, LavaCounts& counts) {
    using L = FluidLattice;
    constexpr int kSteps[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0},
                                  {0, -1, 0}};
    for (int k = 1; k < L::kSide - 1; ++k) {
        for (int i = 1; i < L::kSide - 1; ++i) {
            const CaveColumn& c = l.column(i, k);
            for (int j = 1; j < L::kHeight - 1; ++j) {
                const std::size_t n = L::at(i, j, k);
                const double y = l.y0 + j;
                const bool under =
                    c.lavaLevel > terrain::kDryLevel && y < static_cast<double>(c.lavaLevel);
                const bool underGround = l.rock[n] < -a.groundMargin;
                if (l.lava[n] >= 0.0f) {
                    counts.dryAir += under && underGround && l.carved[n] >= 0.0f ? 1 : 0;
                    continue;
                }
                ++counts.lava;
                counts.inRock += l.carved[n] < 0.0f ? 1 : 0;
                counts.outsideAir += l.carved[n] > -a.skin ? 0 : 1;
                counts.aboveLevel += under ? 0 : 1;
                counts.aboveGround += underGround ? 0 : 1;
                bool againstAir = false;
                bool againstWater = l.water[n] < 0.0f;
                for (int s = 0; s < 6; ++s) {
                    const std::size_t m =
                        L::at(i + kSteps[s][0], j + kSteps[s][1], k + kSteps[s][2]);
                    againstWater = againstWater || l.water[m] < 0.0f;
                    againstAir =
                        againstAir || (s < 4 && l.lava[m] >= 0.0f && l.carved[m] >= 0.0f);
                }
                counts.walls += againstAir ? 1 : 0;
                counts.nearWater += againstWater ? 1 : 0;
            }
        }
    }
}

// Lattices of 1 m around 4 points where the lava shows, in a volcanic world on the relief.
void testPlacement() {
    const terrain::Heightfield heightfield(kSeed, test_relief::legacyRelief());
    const CaveGround ground = caves_test::withEnvironment(
        caves_test::heightfieldGround(heightfield), caves_test::humidEnvironment());
    const CaveField field(kSeed, volcanic(1.0f));
    LavaCounts counts;
    int lattices = 0;
    for (const LavaSite& site : lavaSites(field, ground, 30)) {
        if (!site.visible || lattices == 4) {
            continue;
        }
        ++lattices;
        countLava(readLattice(field, ground, site), field.params().aquifers, counts);
    }
    std::printf("lava: %d lattices, %zu lava samples (%zu in the skin of the rock), %zu over "
                "their level, %zu over the ground, %zu of dry air under it, %zu walls, %zu next "
                "to water\n",
                lattices, counts.lava, counts.inRock, counts.aboveLevel, counts.aboveGround,
                counts.dryAir, counts.walls, counts.nearWater);
    TEST_CHECK(lattices == 4 && counts.lava > 1000);
    TEST_CHECK(counts.outsideAir == 0 && counts.aboveLevel == 0 && counts.aboveGround == 0);
    TEST_CHECK(counts.dryAir == 0 && counts.walls == 0 && counts.nearWater == 0);
}

// In the volcanic regions on land of the streamed world, within 600 m of their sites: at least
// 30 % of the lava cells show their lava in a cavern.
void testVisibility() {
    CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    const terrain::TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {}, params);
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const double size = field.params().aquifers.cellSize;
    int regions = 0;
    int cells = 0;
    int visible = 0;
    for (const terrain::CaveDistrict& d : districts_test::districtsOf(
             field, ground, {-12, 25}, CaveDistrictType::Volcanic, 20.0f)) {
        if (regions == 3) {
            break;
        }
        ++regions;
        const int cx = static_cast<int>(std::floor(d.siteX / size));
        const int cz = static_cast<int>(std::floor(d.siteZ / size));
        for (int n = 0; n < 49; ++n) {
            const AquiferCell cell = field.aquifer(cx + n % 7 - 3, cz + n / 7 - 3, ground);
            if (!cell.lava) {
                continue;
            }
            ++cells;
            visible += inspectLava(field, ground, cell).visible ? 1 : 0;
        }
    }
    const double share = cells == 0 ? 0.0 : static_cast<double>(visible) / cells;
    std::printf("visibility: %d volcanic regions, %d lava cells, %d showing their lava (%.0f%%)\n",
                regions, cells, visible, 100.0 * share);
    TEST_CHECK(regions == 3 && cells >= 20);
    TEST_CHECK(share >= 0.3);
}

} // namespace

int main() {
    testCellsUnchanged();
    testUnderGalleries();
    testPlacement();
    testVisibility();
    return test::result();
}
