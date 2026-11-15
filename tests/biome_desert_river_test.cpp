// The grass of a desert near the water, on its way to the screen (agent 49): along a river of a
// rocky desert of the real planet (the fixture coast_planet, as Unreal loads it; the place
// fleuve_desert of data/dev/places.json), the grass within 150 m of its floor and the stone or
// gravel beyond, in
// - the texels of the tiles of LOD 0 to 3 (TileData::surfaceBlend);
// - the samples of the far horizon on its levels 0 to 5 (the footprints of the levels 4 and 5
//   holding it as a share: a band of 300 m in samples of 128 and 256 m);
// - the channels the GPU reads (Bridge/horizon_blend_texels: blendChannels and back).
// The HLSL draws it as the grass of an oasis (unreal_horizon_blend_scripts_test).
#include "horizon_blend_texels.hpp"

#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::SurfaceMaterial;

// The river of a rocky desert (terrain_biome_places), at the floor of its valley.
constexpr double kRiverX = -29'975'880.0;
constexpr double kRiverZ = -5'858'190.0;

float grassOf(const terrain::SurfaceBlend& b) {
    float grass = 0.0f;
    for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
        grass += b.materials[s] == SurfaceMaterial::Grass ? b.materialWeights[s] : 0.0f;
    }
    return grass;
}

bool desertOf(const terrain::SurfaceBlend& b) {
    return terrain::isDesert(b.biomes[0]);
}

// The texels of the tiles of a LOD around the river: some grass in a desert near it.
int grassyTexels(const terrain::TerrainTiles& tiles, int lod) {
    int grassy = 0;
    // Tiles every 48 m at least, 3 on each side: the bands from the bank to 150 m on LOD 0 too.
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            const double step = std::max(terrain::tileSizeMeters(lod), 48.0);
            const terrain::ChunkCoord chunk{
                static_cast<std::int32_t>(std::floor((kRiverX + dx * step) / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor((kRiverZ + dz * step) / terrain::kChunkSizeMeters))};
            const terrain::TileData data = tiles.generate({terrain::tileContaining(chunk, lod), 0});
            for (const terrain::BlendTexel& texel : data.surfaceBlend) {
                const terrain::SurfaceBlend b = terrain::unpackBlend(texel);
                grassy += desertOf(b) && grassOf(b) > 0.0f ? 1 : 0;
            }
        }
    }
    return grassy;
}

// The samples of a level of the horizon around the river: the most grass of a desert sample, and
// the same read back from the channels of the GPU.
void horizonGrass(const terrain::TerrainTiles& tiles, int level, float& most, float& gpu) {
    const terrain::HorizonParams params;
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    const int pitch = terrain::levelPitch(params, level);
    const auto i = static_cast<std::int64_t>(std::floor(kRiverX / pitch));
    const auto k = static_cast<std::int64_t>(std::floor(kRiverZ / pitch));
    const std::int64_t r = std::max<std::int64_t>(2, 2048 / pitch);
    most = 0.0f;
    gpu = 0.0f;
    for (const terrain::HorizonCell& cell : sampler.sample(level, {i - r, k - r, i + r + 1, k + r + 1})) {
        const terrain::SurfaceBlend b = terrain::unpackBlend(cell.blend);
        if (!desertOf(b) || !(grassOf(b) > most)) {
            continue;
        }
        most = grassOf(b);
        const auto back = terrain::unreal::blendOfChannels(terrain::unreal::blendChannels(cell.blend));
        gpu = back ? grassOf(terrain::unpackBlend(*back)) : -1.0f;
    }
}

} // namespace

int main() {
    const terrain::TerrainTiles& tiles = coast_planet::reliefTiles();
    // The column itself: a rocky desert, grass 100 m from the river, stone or gravel 400 m away.
    TEST_CHECK(terrain::isDesert(terrain::unpackBlend(tiles.generate(
                   {terrain::tileContaining({static_cast<std::int32_t>(std::floor(
                                                 kRiverX / terrain::kChunkSizeMeters)),
                                             static_cast<std::int32_t>(std::floor(
                                                 kRiverZ / terrain::kChunkSizeMeters))},
                                            0),
                    0}).surfaceBlend[12]).biomes[0]));
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const int grassy = grassyTexels(tiles, lod);
        std::printf("LOD %d: %d texels of the desert with grass near the river\n", lod, grassy);
        TEST_CHECK(grassy > 0);
    }
    for (int level = 0; level <= 5; ++level) {
        float most = 0.0f;
        float gpu = 0.0f;
        horizonGrass(tiles, level, most, gpu);
        std::printf("horizon level %d: grass up to %.2f of a desert sample, %.2f on the GPU\n",
                    level, static_cast<double>(most), static_cast<double>(gpu));
        TEST_CHECK(most > 0.0f && gpu == most);
    }
    return test::result();
}
