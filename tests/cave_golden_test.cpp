// Golden checksums of the caves of Phases 8A and 8B, captured before the cave districts (8C):
// without districts the caves must stay bit-identical. One FNV-1a checksum per part, so that a
// failure says what changed: voids, densities and water of LOD 0 to 2 at fixed points, aquifer
// cells, entrances, speleothems, streamed tiles and materials. Floating-point results depend on
// the compiler and the target (contractions, libm), so the checksums are only asserted on the
// platform they were captured on (x86-64 without FMA, GCC or Clang) and printed elsewhere.
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(__x86_64__) && !defined(__FMA__) && (defined(__GNUC__) || defined(__clang__))
#define TERRAIN_GOLDEN_PLATFORM 1
#else
#define TERRAIN_GOLDEN_PLATFORM 0
#endif

namespace {

using terrain::CaveColumn;
using terrain::CaveEntrance;
using terrain::CaveGround;
using terrain::CaveSampler;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// 64-bit FNV-1a over the raw bits of the values added.
class Checksum {
public:
    void add(std::uint64_t value, int bytes = 8) {
        for (int i = 0; i < bytes; ++i) {
            hash_ = (hash_ ^ ((value >> (8 * i)) & 0xFFU)) * 0x100000001B3ULL;
        }
    }
    void add(float value) { add(std::bit_cast<std::uint32_t>(value), 4); }
    void add(double value) { add(std::bit_cast<std::uint64_t>(value)); }
    void add(int value) { add(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)), 4); }
    void add(bool value) { add(static_cast<std::uint64_t>(value ? 1 : 0), 1); }
    void add(const terrain::Vec3f& v) {
        add(v.x);
        add(v.y);
        add(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

// A temperate, humid climate everywhere: wet aquifers and speleothems without the cost of the
// climate fields.
CaveGround groundOf(const terrain::Heightfield& field) {
    CaveGround ground;
    ground.height = [&field](double x, double z) { return field.heightAt(x, z); };
    ground.environment = [](double, double) {
        return terrain::CaveEnvironment{terrain::Biome::TemperateForest, 11.0f, 0.75f};
    };
    return ground;
}

double unit(std::uint64_t stream, std::uint64_t index) {
    return static_cast<double>(terrain::noise::deriveSeed(stream, index) >> 40) / 16777216.0;
}

void addVoids(Checksum& sum, const terrain::CaveVoids& v) {
    for (const float value : {v.spaghetti, v.noodles, v.cheese, v.ravines, v.entrances, v.limit,
                              v.entranceLimit, v.barrier}) {
        sum.add(value);
    }
}

void addColumn(Checksum& sum, const CaveColumn& c) {
    sum.add(c.ground);
    sum.add(c.exclusion);
    sum.add(c.ravineDistance);
    sum.add(c.ravineStrength);
    sum.add(c.entranceCount);
    sum.add(c.waterLevel);
    sum.add(c.frozen);
    sum.add(c.barrierCount);
}

// Voids, combined void, carved density and water at 2500 points of each of 8 boxes of 64 m,
// for the detail of a LOD.
std::uint64_t pointsChecksum(const terrain::CaveField& field, const CaveGround& ground, int lod) {
    Checksum sum;
    const terrain::CaveDetail detail = terrain::caveDetailForLod(lod);
    for (std::uint64_t box = 0; box < 8; ++box) {
        const double x0 = -2600.0 + 700.0 * static_cast<double>(box);
        const double z0 = -4100.0 + 450.0 * static_cast<double>(box % 3);
        CaveSampler sampler(field, ground, detail, {x0, -1200.0, z0, x0 + 64.0, 1600.0, z0 + 64.0});
        for (std::uint64_t n = 0; n < 2500; ++n) {
            const double x = x0 + 64.0 * unit(box, 3 * n);
            const double z = z0 + 64.0 * unit(box, 3 * n + 1);
            const float height = ground.height(x, z);
            const double y = static_cast<double>(height) - 105.0 + 110.0 * unit(box, 3 * n + 2);
            const CaveColumn column = sampler.column(x, z, height);
            const auto terrainDensity = static_cast<float>(y - static_cast<double>(height));
            const float carved = sampler.density(column, y, terrainDensity);
            addColumn(sum, column);
            addVoids(sum, sampler.voids(column, y));
            sum.add(sampler.voidAt(column, y));
            sum.add(carved);
            sum.add(sampler.waterDensity(column, y, terrainDensity, carved));
        }
    }
    return sum.value();
}

std::uint64_t aquifersChecksum(const terrain::CaveField& field, const CaveGround& ground) {
    Checksum sum;
    for (int cellZ = -6; cellZ < 6; ++cellZ) {
        for (int cellX = -6; cellX < 6; ++cellX) {
            const terrain::AquiferCell cell = field.aquifer(cellX, cellZ, ground);
            sum.add(cell.siteX);
            sum.add(cell.siteZ);
            sum.add(cell.wet);
            sum.add(cell.frozen);
            sum.add(cell.level);
        }
    }
    return sum.value();
}

void addEntrance(Checksum& sum, const CaveEntrance& e) {
    for (const double value : {e.topX, e.topY, e.topZ, e.bottomX, e.bottomY, e.bottomZ}) {
        sum.add(value);
    }
    sum.add(e.tubeRadius);
    sum.add(e.chamberRadius);
    sum.add(e.cellX);
    sum.add(e.cellZ);
}

void addSpeleothem(Checksum& sum, const terrain::Speleothem& s) {
    sum.add(static_cast<std::uint64_t>(s.kind), 1);
    sum.add(s.x);
    sum.add(s.z);
    sum.add(s.y);
    sum.add(s.length);
    sum.add(s.radius);
    sum.add(s.ice);
}

void addMesh(Checksum& sum, const terrain::Mesh& mesh) {
    sum.add(static_cast<std::uint64_t>(mesh.vertices.size()));
    for (const terrain::Vec3f& v : mesh.vertices) {
        sum.add(v);
    }
    for (const terrain::Vec3f& n : mesh.normals) {
        sum.add(n);
    }
    for (const std::uint32_t index : mesh.indices) {
        sum.add(static_cast<std::uint64_t>(index), 4);
    }
}

void addTile(Checksum& sum, const terrain::TileData& data) {
    sum.add(data.originX);
    sum.add(data.originY);
    sum.add(data.originZ);
    addMesh(sum, data.mesh);
    sum.add(static_cast<std::uint64_t>(data.plants.size()));
    for (const terrain::Plant& p : data.plants) {
        sum.add(p.x);
        sum.add(p.z);
        sum.add(p.y);
    }
    for (const terrain::CaveMaterial m : data.materials) {
        sum.add(static_cast<std::uint64_t>(m), 1);
    }
    addMesh(sum, data.water);
    for (const terrain::CaveMaterial m : data.waterMaterials) {
        sum.add(static_cast<std::uint64_t>(m), 1);
    }
    sum.add(static_cast<std::uint64_t>(data.speleothems.size()));
    for (const terrain::Speleothem& s : data.speleothems) {
        addSpeleothem(sum, s);
    }
}

std::uint64_t tilesChecksum(const CaveEntrance& entrance) {
    const terrain::TerrainTiles tiles(kSeed, {}, {}, terrain::CaveParams{});
    const terrain::ChunkCoord chunk = terrain::worldToChunk(entrance.bottomX, entrance.bottomZ);
    constexpr unsigned kAllSeams = 0xFFU;
    Checksum sum;
    addTile(sum, tiles.generate({{chunk, 0}, kAllSeams}));
    addTile(sum, tiles.generate({terrain::tileContaining(chunk, 1), terrain::kSeamNegX}));
    addTile(sum, tiles.generate({terrain::tileContaining(chunk, 2), 0}));
    return sum.value();
}

std::uint64_t materialsChecksum() {
    Checksum sum;
    for (int biome = 0; biome < terrain::kBiomeCount; ++biome) {
        for (const float temperature : {-12.0f, -2.0f, 4.0f, 18.0f, 30.0f}) {
            for (const float humidity : {0.05f, 0.45f, 0.62f, 0.95f}) {
                for (const float depth : {0.5f, 10.0f, 25.0f, 50.0f, 80.0f}) {
                    for (const float normalY : {-0.9f, 0.0f, 0.8f}) {
                        terrain::CaveWallSite site;
                        site.environment = {static_cast<terrain::Biome>(biome), temperature,
                                            humidity};
                        site.depth = depth;
                        site.normalY = normalY;
                        site.aboveWater = depth > 40.0f ? 1.0f : 1.0e9f;
                        site.frozenWater = temperature < 0.0f;
                        sum.add(static_cast<std::uint64_t>(terrain::caveMaterial(site)), 1);
                    }
                }
            }
        }
    }
    return sum.value();
}

struct Part {
    const char* name;
    std::uint64_t actual;
    std::uint64_t expected;
};

} // namespace

int main() {
    const terrain::Heightfield heightfield(kSeed);
    const CaveGround ground = groundOf(heightfield);
    const terrain::CaveField field(kSeed);
    const std::vector<CaveEntrance> entrances =
        field.entrancesNear(-750.0, -4750.0, 750.0, -3250.0, ground);
    TEST_CHECK(!entrances.empty());
    if (entrances.empty()) {
        return test::result();
    }
    Checksum entranceSum;
    for (const CaveEntrance& e : entrances) {
        addEntrance(entranceSum, e);
    }
    const CaveEntrance& first = entrances.front();
    Checksum speleothemSum;
    std::size_t speleothemCount = 0;
    {
        constexpr double x0 = 1000.0;
        constexpr double z0 = -2000.0;
        constexpr double side = 96.0;
        CaveSampler sampler(field, ground, terrain::caveDetailForLod(0),
                            {x0, -1200.0, z0, x0 + side, 4000.0, z0 + side});
        const terrain::SpeleothemField speleothems(kSeed);
        const std::vector<terrain::Speleothem> placed =
            speleothems.place(sampler, x0, z0, x0 + side, z0 + side);
        for (const terrain::Speleothem& s : placed) {
            addSpeleothem(speleothemSum, s);
            ++speleothemCount;
        }
    }
    TEST_CHECK(speleothemCount > 10);
    const Part parts[] = {
        {"points LOD 0", pointsChecksum(field, ground, 0), 0xC1B49F441628B500ULL},
        {"points LOD 1", pointsChecksum(field, ground, 1), 0x847399965D4D927AULL},
        {"points LOD 2", pointsChecksum(field, ground, 2), 0x2A7153C19E0A1837ULL},
        {"aquifers", aquifersChecksum(field, ground), 0x49504A9D474AA707ULL},
        {"entrances", entranceSum.value(), 0x7D153494DDDA179EULL},
        {"speleothems", speleothemSum.value(), 0x8E535CD1327FED4AULL},
        {"tiles", tilesChecksum(first), 0x41D8239DBF1E14C7ULL},
        {"materials", materialsChecksum(), 0xD9C5568AB1EF9B55ULL},
    };
    std::printf("golden checksums (%zu entrances, %zu speleothems)%s:\n", entrances.size(),
                speleothemCount,
                TERRAIN_GOLDEN_PLATFORM ? "" : ", not asserted on this platform");
    for (const Part& part : parts) {
        std::printf("  %-12s 0x%016" PRIX64 "ULL%s\n", part.name, part.actual,
                    part.actual == part.expected ? "" : "  (differs)");
        if (TERRAIN_GOLDEN_PLATFORM) {
            TEST_CHECK(part.actual == part.expected);
        }
    }
    return test::result();
}
