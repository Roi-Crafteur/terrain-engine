// Self-check of the Unreal plugin: the tables of heights and tile fingerprints hold what the core
// built by CMake computes (when the values of the terrain change, the tables must follow in the
// same commit), a fingerprint changes with any bit of a mesh, and the check passes on the default
// world and names the first probe that differs on another world.
#include "determinism_check.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace {

using terrain::Heightfield;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::unreal::DeterminismReport;
using terrain::unreal::HeightProbe;
using terrain::unreal::TileProbe;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Prints the bits computed for each height probe that differs, to update the table.
void testHeightTableMatchesCore() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    bool matching = true;
    for (const HeightProbe& probe : terrain::unreal::heightProbes()) {
        const auto bits = std::bit_cast<std::uint32_t>(field.heightAt(probe.x, probe.z));
        if (bits != probe.bits) {
            matching = false;
            std::fprintf(stderr, "probe (%.10g, %.10g): 0x%08XU expected, 0x%08XU computed\n",
                         probe.x, probe.z, static_cast<unsigned>(probe.bits),
                         static_cast<unsigned>(bits));
        }
    }
    TEST_CHECK(matching);
}

// Prints the fingerprint computed for each tile probe that differs, to update the table.
void testTileTableMatchesCore() {
    const TerrainTiles tiles(terrain::kDefaultWorldSeed);
    bool matching = true;
    for (const TileProbe& probe : terrain::unreal::tileProbes()) {
        const TileData data = tiles.generate(probe.request);
        TEST_CHECK(!data.mesh.indices.empty());
        const std::uint64_t fingerprint = terrain::unreal::tileFingerprint(data);
        if (fingerprint != probe.fingerprint) {
            matching = false;
            std::fprintf(stderr, "tile (%d, %d): 0x%016llXULL expected, 0x%016llXULL computed\n",
                         probe.request.key.origin.x, probe.request.key.origin.z,
                         static_cast<unsigned long long>(probe.fingerprint),
                         static_cast<unsigned long long>(fingerprint));
        }
    }
    TEST_CHECK(matching);
}

// The probes cover the planet, negative and fractional coordinates, land, sea and mountains; the
// tiles are LOD 0, one with seams.
void testProbesCover() {
    const auto heights = terrain::unreal::heightProbes();
    TEST_CHECK(heights.size() >= 8);
    const auto has = [&heights](bool (*property)(const HeightProbe&)) {
        return std::any_of(heights.begin(), heights.end(), property);
    };
    TEST_CHECK(has([](const HeightProbe& p) { return std::abs(p.x) > 2.5e7; }));
    TEST_CHECK(has([](const HeightProbe& p) { return p.x < 0.0 && p.z < 0.0; }));
    TEST_CHECK(has([](const HeightProbe& p) { return p.x != std::floor(p.x); }));
    TEST_CHECK(has([](const HeightProbe& p) { return std::bit_cast<float>(p.bits) < -100.0f; }));
    TEST_CHECK(has([](const HeightProbe& p) { return std::bit_cast<float>(p.bits) > 1000.0f; }));
    const auto tiles = terrain::unreal::tileProbes();
    TEST_CHECK(tiles.size() >= 2);
    TEST_CHECK(std::all_of(tiles.begin(), tiles.end(),
                           [](const TileProbe& p) { return p.request.key.lod == 0; }));
    TEST_CHECK(std::any_of(tiles.begin(), tiles.end(),
                           [](const TileProbe& p) { return p.request.seams != 0; }));
}

// One bit of a vertex, of a normal, of the origin, or the order of two indices changes the
// fingerprint; the same mesh keeps it.
void testFingerprintSensitive() {
    const TerrainTiles tiles(terrain::kDefaultWorldSeed);
    const TileData data = tiles.generate(terrain::unreal::tileProbes()[1].request);
    const std::uint64_t original = terrain::unreal::tileFingerprint(data);
    TEST_CHECK(terrain::unreal::tileFingerprint(TileData(data)) == original);
    TileData vertex = data;
    vertex.mesh.vertices[7].y = std::nextafter(vertex.mesh.vertices[7].y, 1e9f);
    TEST_CHECK(terrain::unreal::tileFingerprint(vertex) != original);
    TileData normal = data;
    normal.mesh.normals[3].x = std::nextafter(normal.mesh.normals[3].x, -1.0f);
    TEST_CHECK(terrain::unreal::tileFingerprint(normal) != original);
    TileData origin = data;
    origin.originY = std::nextafter(origin.originY, 1e9);
    TEST_CHECK(terrain::unreal::tileFingerprint(origin) != original);
    TileData order = data;
    std::swap(order.mesh.indices[0], order.mesh.indices[1]);
    TEST_CHECK(terrain::unreal::tileFingerprint(order) != original);
}

void testDefaultWorldPasses() {
    const DeterminismReport report = terrain::unreal::checkDefaultWorld();
    TEST_CHECK(report.same);
    TEST_CHECK(contains(terrain::unreal::describe(report), "match"));
}

// Another world differs: the reports name the first probe that differs, with both values.
void testOtherWorldFails() {
    const TerrainTiles other(terrain::kDefaultWorldSeed + 1);
    const DeterminismReport heights = terrain::unreal::checkHeights(other.heightfield());
    TEST_CHECK(!heights.same && !heights.tile);
    const auto probes = terrain::unreal::heightProbes();
    const auto first = std::find_if(probes.begin(), probes.end(), [&other](const HeightProbe& p) {
        return std::bit_cast<std::uint32_t>(other.heightfield().heightAt(p.x, p.z)) != p.bits;
    });
    TEST_CHECK(first != probes.end());
    if (first != probes.end()) {
        TEST_CHECK(heights.height.x == first->x && heights.height.z == first->z);
        TEST_CHECK(heights.actualBits ==
                   std::bit_cast<std::uint32_t>(other.heightfield().heightAt(first->x, first->z)));
    }
    const std::string text = terrain::unreal::describe(heights);
    TEST_CHECK(text.starts_with("this build does not generate the same world: heightAt"));
    TEST_CHECK(contains(text, "instead of"));

    const DeterminismReport tiles = terrain::unreal::checkTiles(other);
    TEST_CHECK(!tiles.same && tiles.tile);
    const TileProbe& firstTile = terrain::unreal::tileProbes()[0];
    TEST_CHECK(tiles.tileProbe.request == firstTile.request);
    TEST_CHECK(tiles.actualFingerprint ==
               terrain::unreal::tileFingerprint(other.generate(firstTile.request)));
    const std::string tileText = terrain::unreal::describe(tiles);
    TEST_CHECK(tileText.starts_with("this build does not generate the same world: the mesh"));
    TEST_CHECK(contains(tileText, "(0, 0) LOD 0"));
}

} // namespace

int main() {
    testHeightTableMatchesCore();
    testTileTableMatchesCore();
    testProbesCover();
    testFingerprintSensitive();
    testDefaultWorldPasses();
    testOtherWorldFails();
    return test::result();
}
