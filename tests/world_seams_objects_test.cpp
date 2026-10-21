// The objects of the grids across the edges of the wrapped world (WorldSize): the entrances,
// speleothems, cave flora and plants of an area across the edge x = world.x / 2 (or the corner)
// are exactly those of its two pieces, the piece beyond the edge placed in the canonical range
// and moved by a whole world: no duplicate and no hole. The arch candidates of a cell and of its
// images are the same, moved by a whole world.
#include "terrain/generation/arches.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_environment_test_support.hpp"
#include "cave_flora_test_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "speleothems_test_support.hpp"
#include "test_assert.hpp"
#include "world_seams_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <vector>

namespace {

using seams_test::kWorld;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr double kEdgeX = seams_test::kWorld.x / 2.0;
constexpr double kCornerZ = seams_test::kWorld.z / 2.0;
constexpr double kRowZ = -10268384.0; // land at a southern mid-latitude, on the edge x

// [minX, maxX) x [minZ, maxZ).
struct Rect {
    double minX;
    double minZ;
    double maxX;
    double maxZ;
};

// The objects of a rectangle across the edge x = world.x / 2, placed at once, against its two
// pieces: the west one as is, the east one placed in the canonical range (from -world.x / 2) and
// moved back by a whole world.
template <typename Place, typename Move>
bool sameAsPieces(const Rect& whole, Place place, Move move, std::size_t& count) {
    auto all = place(whole);
    auto pieces = place(Rect{whole.minX, whole.minZ, kEdgeX, whole.maxZ});
    auto east = place(Rect{-kEdgeX, whole.minZ, whole.maxX - kWorld.x, whole.maxZ});
    for (auto& object : east) {
        move(object);
    }
    pieces.insert(pieces.end(), east.begin(), east.end());
    const auto order = [](const auto& a, const auto& b) {
        return std::tuple(a.z, a.x, a.y) < std::tuple(b.z, b.x, b.y); // a stalactite above
    };
    std::sort(all.begin(), all.end(), order);
    std::sort(pieces.begin(), pieces.end(), order);
    count = all.size();
    return all == pieces;
}

// A sampler of the caves under flat ground of this height over the rectangle, from the deepest
// caves to the ground.
terrain::CaveSampler samplerOver(const terrain::CaveField& field, const terrain::CaveGround& ground,
                                 float height, const Rect& r) {
    const double depth = static_cast<double>(field.params().maxDepth) + 6.0;
    return terrain::CaveSampler(field, ground, terrain::CaveDetail{},
                                {r.minX, height - depth, r.minZ, r.maxX, height + 4.0, r.maxZ});
}

// Speleothems of a rectangle across the edge, with a candidate in every cell, on flat ground.
void testSpeleothems() {
    terrain::CaveParams params = speleothems_test::aquiferParams(false);
    params.speleothems.chance = 1.0f;
    params.speleothems.dryFactor = 1.0f;
    const terrain::CaveField field(kSeed, params);
    const terrain::SpeleothemField speleothems(kSeed, params.speleothems);
    const terrain::CaveGround ground = caves_test::withEnvironment(
        caves_test::flatGround(speleothems_test::kFlatHeight), caves_test::humidEnvironment());
    const auto place = [&](const Rect& r) {
        terrain::CaveSampler sampler = samplerOver(field, ground, speleothems_test::kFlatHeight, r);
        return speleothems.place(sampler, r.minX, r.minZ, r.maxX, r.maxZ);
    };
    std::size_t count = 0;
    const bool same = sameAsPieces(
        Rect{kEdgeX - 23.0, kRowZ, kEdgeX + 25.0, kRowZ + 48.0}, place,
        [](terrain::Speleothem& s) { s.x += kWorld.x; }, count);
    std::printf("speleothems: %zu across the edge, those of its two pieces\n", count);
    TEST_CHECK(same && count > 10);
}

// Cave flora of a rectangle across the edge, in caves whose every district is lush.
void testCaveFlora() {
    const terrain::CaveParams params = flora_test::lushWorld(false);
    const terrain::CaveField field(kSeed, params);
    const terrain::CaveFloraField flora(kSeed, params.districts->flora);
    const terrain::CaveGround ground = flora_test::flatWorld(flora_test::mildEnvironment());
    const auto place = [&](const Rect& r) {
        terrain::CaveSampler sampler = samplerOver(field, ground, 300.0f, r); // flatWorld
        return flora.place(sampler, r.minX, r.minZ, r.maxX, r.maxZ);
    };
    std::size_t count = 0;
    const bool same = sameAsPieces(
        Rect{kEdgeX - 41.0, kRowZ, kEdgeX + 39.0, kRowZ + 80.0}, place,
        [](terrain::CaveFlora& f) { f.x += kWorld.x; }, count);
    std::printf("cave flora: %zu plants across the edge, those of its two pieces\n", count);
    TEST_CHECK(same && count > 10);
}

// Plants of a rectangle across the edge on the heightfield, each piece with its own ecology map
// on the lattice of 4 m of the streamed tiles (x and z = 2 modulo 4), one sample beyond its sides.
void testPlants() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    const terrain::Ecology ecology(kSeed);
    const terrain::ReliefHeight surface = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    const auto place = [&](const Rect& r) {
        const double sizeX = r.maxX - r.minX;
        const double sizeZ = r.maxZ - r.minZ;
        const double side = std::max(sizeX, sizeZ) + 8.0;
        const terrain::HeightmapRegion region{r.minX - 4.0 + side / 2.0, r.minZ - 4.0 + side / 2.0,
                                              4.0, static_cast<int>(side / 4.0)};
        const terrain::EcologyMap map = ecology.map(terrain::surfaceFromHeightfield(field, region));
        return ecology.placePlants(map, surface, {r.minX, r.minZ, r.maxX, r.maxZ});
    };
    std::size_t count = 0;
    const bool same = sameAsPieces(
        Rect{kEdgeX - 32.0, kRowZ, kEdgeX + 32.0, kRowZ + 64.0}, place,
        [](terrain::Plant& p) { p.x += kWorld.x; }, count);
    std::printf("plants: %zu across the edge, those of its two pieces\n", count);
    TEST_CHECK(same && count > 10);
}

// Entrances whose reach meets an area across the corner, against those of its pieces on both
// sides of the edge x (each entrance once, its cell given in the frame of the area).
void testEntrances() {
    const terrain::TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {},
                                      terrain::CaveParams{});
    const terrain::CaveField& caves = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    const auto cells = static_cast<int>(caves.entranceCellsX().count);
    const auto order = [](const terrain::CaveEntrance& a, const terrain::CaveEntrance& b) {
        return std::tuple(a.cellZ, a.cellX) < std::tuple(b.cellZ, b.cellX);
    };
    bool same = true;
    std::size_t count = 0;
    for (const double z : {kRowZ, kCornerZ}) {
        std::vector<terrain::CaveEntrance> all =
            caves.entrancesNear(kEdgeX - 1500.0, z - 1500.0, kEdgeX + 1300.0, z + 1500.0, ground);
        std::vector<terrain::CaveEntrance> pieces =
            caves.entrancesNear(kEdgeX - 1500.0, z - 1500.0, kEdgeX, z + 1500.0, ground);
        for (terrain::CaveEntrance e :
             caves.entrancesNear(-kEdgeX, z - 1500.0, -kEdgeX + 1300.0, z + 1500.0, ground)) {
            e.topX += kWorld.x;
            e.bottomX += kWorld.x;
            e.cellX += cells;
            if (std::find(pieces.begin(), pieces.end(), e) == pieces.end()) {
                pieces.push_back(e);
            }
        }
        std::sort(all.begin(), all.end(), order);
        std::sort(pieces.begin(), pieces.end(), order);
        same = same && all == pieces;
        count += all.size();
    }
    std::printf("entrances: %zu across the edge and the corner, those of the pieces\n", count);
    TEST_CHECK(same && count > 10);
}

// The arch candidates of the cells around the corner, at three images of their index.
void testArches() {
    const terrain::ArchField arches(kSeed);
    const auto nx = static_cast<int>(arches.cellsX().count);
    const auto nz = static_cast<int>(arches.cellsZ().count);
    bool same = true;
    int found = 0;
    for (int k = nz / 2 - 3; k < nz / 2 + 3; ++k) {
        for (int i = nx / 2 - 3; i < nx / 2 + 3; ++i) {
            terrain::Arch arch;
            const bool has = arches.candidate(i, k, arch);
            found += has ? 1 : 0;
            for (const auto& image : {std::array<int, 2>{1, 0}, {-1, 2}, {3, -1}}) {
                terrain::Arch other;
                same = same && arches.candidate(i + image[0] * nx, k + image[1] * nz, other) == has;
                if (has) {
                    other.x -= image[0] * kWorld.x;
                    other.z -= image[1] * kWorld.z;
                    other.cellX -= image[0] * nx;
                    other.cellZ -= image[1] * nz;
                    same = same && std::abs(other.x - arch.x) < 1e-6 &&
                           std::abs(other.z - arch.z) < 1e-6;
                    other.x = arch.x;
                    other.z = arch.z;
                    same = same && other == arch;
                }
            }
        }
    }
    std::printf("arches: %d candidates around the corner, the same at every image\n", found);
    TEST_CHECK(same && found > 0);
}

} // namespace

int main() {
    testSpeleothems();
    testCaveFlora();
    testPlants();
    testEntrances();
    testArches();
    return test::result();
}
