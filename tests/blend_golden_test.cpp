// Golden checksums of the transitions between the biomes and the materials, asserted on every
// platform (see golden_support.hpp): the blends of the Ecology on surfaces of the Heightfield at
// 50 m and 2 km, near a mountain, the origin and the corner of the wrapped world, and the
// texels of the surface blend of the tiles of LOD 0 to 3 holding a mountain chunk, with and
// without seams, and at the corner. Additions: the checksums of the existing parts
// (world_golden_test, heightfield_golden_test) do not change.

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using golden::Checksum;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr double kCornerX = terrain::WorldSize{}.x / 2.0;
constexpr double kCornerZ = terrain::WorldSize{}.z / 2.0;

void addBlend(Checksum& sum, const terrain::SurfaceBlend& b) {
    for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
        sum.add(static_cast<std::uint64_t>(b.biomes[s]), 1);
        sum.add(b.biomeWeights[s]);
        sum.add(static_cast<std::uint64_t>(b.materials[s]), 1);
        sum.add(b.materialWeights[s]);
    }
}

void addTexels(Checksum& sum, const terrain::TileData& data) {
    sum.add(static_cast<std::uint64_t>(data.surfaceBlend.size()));
    for (const terrain::BlendTexel& t : data.surfaceBlend) {
        sum.add(static_cast<std::uint64_t>(t.ids), 4);
        sum.add(static_cast<std::uint64_t>(t.weights), 4);
    }
}

// The blends of a surface of 48 x 48 samples.
std::uint64_t blendChecksum(const terrain::Heightfield& field, const terrain::Ecology& ecology,
                            double centerX, double centerZ, double metersPerPixel) {
    const terrain::TerrainSurface surface =
        terrain::surfaceFromHeightfield(field, {centerX, centerZ, metersPerPixel, 48});
    Checksum sum;
    for (const terrain::SurfaceBlend& b : ecology.blendMap(surface, ecology.map(surface))) {
        addBlend(sum, b);
    }
    return sum.value();
}

std::vector<golden::Part> blendParts() {
    const terrain::TerrainTiles tiles(kSeed);
    const terrain::Heightfield& field = tiles.heightfield();
    const terrain::Ecology& ecology = tiles.ecology();
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(field);
    const double mx = mountain.x * terrain::kChunkSizeMeters;
    const double mz = mountain.z * terrain::kChunkSizeMeters;
    std::vector<golden::Part> parts = {
        {"blend 50 m", blendChecksum(field, ecology, mx, mz, 50.0), 0x126FD8A815AC73E6ULL},
        {"blend 2 km", blendChecksum(field, ecology, 0.0, 0.0, 2000.0), 0x59A0EF3946387CD8ULL},
        {"corner blend 50 m", blendChecksum(field, ecology, kCornerX, kCornerZ, 50.0),
         0x2ABB6B5E157FDD25ULL},
    };
    constexpr std::array<std::uint64_t, terrain::kLodCount> kExpected = {
        0xD148F2B63B104A25ULL, 0x39C9B7288AF5F6E9ULL, 0xC28BC91C460C9089ULL, 0x02E1D54B2FA88CEDULL};
    constexpr std::array<const char*, terrain::kLodCount> kNames = {
        "tile blend LOD 0", "tile blend LOD 1", "tile blend LOD 2", "tile blend LOD 3"};
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileKey key = terrain::tileContaining(mountain, lod);
        Checksum sum;
        addTexels(sum, tiles.generate({key, 0}));
        addTexels(sum, tiles.generate({key, terrain::kSeamPosX | terrain::kSeamNegXNegZ}));
        const auto index = static_cast<std::size_t>(lod);
        parts.push_back({kNames[index], sum.value(), kExpected[index]});
    }
    const terrain::ChunkCoord count = terrain::worldChunks(tiles.world());
    const terrain::ChunkCoord corner{count.x / 2 - 1, count.z / 2 - 1};
    Checksum cornerSum;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        addTexels(cornerSum, tiles.generate({terrain::tileContaining(corner, lod), 0}));
    }
    parts.push_back({"tile blend at the corner", cornerSum.value(), 0xAC68A51875AB48A5ULL});
    return parts;
}

} // namespace

int main() {
    golden::report("biome and material transitions", blendParts());
    return test::result();
}
