// The water the surface blend of the tiles reads (agent 49): the texels of the tiles take the water
// of the core at their columns (TileColumns::waterKinds, waterLevels), as the samples of the far
// horizon do (horizon_blend.cpp: pointSite). On the default world with its planet file (the
// fixture coast_planet, as Unreal loads it):
// - a dry depression below the sea level is no ocean: its texels are neither the ocean biome nor
//   water (before, the tiles took any ground below 0 m for the sea);
// - the lakes and the rivers of the hydrology are water in the texels, as in the samples;
// - level 0 of the horizon is the texels of the LOD 3 tiles bit for bit at these places too.
#include "terrain/generation/biome_blend.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::BlendTexel;
using terrain::HorizonCell;
using terrain::HorizonParams;
using terrain::HorizonRect;
using terrain::SurfaceBlend;

struct Place {
    const char* name;
    double x;
    double z;
};

// Places of data/dev/places.json: the floor and the rim of the dry depression n. 27, a river of
// the plains, a lake of the mountains, the basin of the fall of 388 m.
constexpr Place kPlaces[] = {
    {"depression_seche_fond", -410'000.0, 4'589'844.0},
    {"depression_seche_haut", -380'000.0, 4'589'844.0},
    {"fleuve_plaine", -241'631.0, -86'277.0},
    {"lac_montagne", 7'369'228.0, -7'088'911.0},
    {"chute388_bassin", -6'185'104.0, 155'924.0},
};

struct Counts {
    int texels = 0;
    int same = 0;     // texels equal to the sample of level 0 at their point
    int dryBelow = 0; // texels on dry ground below the sea level
    int dryBelowOcean = 0;
    int inland = 0; // texels under a lake or a river
    int inlandWater = 0;
};

terrain::ChunkCoord chunkOf(double x, double z) {
    return {static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
}

// The LOD 3 tiles of a 3 x 3 block around a place, against the samples of level 0 at their texels.
void comparePlace(const Place& place, Counts& counts) {
    const terrain::TerrainTiles& tiles = coast_planet::reliefTiles();
    const HorizonParams params;
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    const int level0 = terrain::levelPitch(params, 0);
    const terrain::ChunkCoord center = chunkOf(place.x, place.z);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const terrain::ChunkCoord chunk{center.x + 8 * dx, center.z + 8 * dz};
            const terrain::TileData data = tiles.generate({terrain::tileContaining(chunk, 3), 0});
            const auto x0 = static_cast<std::int64_t>(data.originX / level0);
            const auto z0 = static_cast<std::int64_t>(data.originZ / level0);
            const HorizonRect rect{x0, z0, x0 + 4 * 32 + 1, z0 + 4 * 32 + 1};
            const std::vector<HorizonCell> cells = sampler.sample(0, rect);
            for (int k = 0; k < terrain::kTileBlendTexels; ++k) {
                for (int i = 0; i < terrain::kTileBlendTexels; ++i) {
                    const BlendTexel texel =
                        data.surfaceBlend[static_cast<std::size_t>(k * terrain::kTileBlendTexels + i)];
                    const HorizonCell& cell =
                        cells[static_cast<std::size_t>(4 * k * rect.width() + 4 * i)];
                    const SurfaceBlend blend = terrain::unpackBlend(texel);
                    ++counts.texels;
                    counts.same += cell.blend == texel ? 1 : 0;
                    const bool dry = (cell.flags & terrain::kHorizonWater) == 0;
                    if (dry && cell.height < terrain::kSeaLevel) {
                        ++counts.dryBelow;
                        counts.dryBelowOcean +=
                            blend.biomes[0] == terrain::Biome::Ocean ||
                                    blend.materials[0] == terrain::SurfaceMaterial::Water
                                ? 1
                                : 0;
                    }
                    if (!dry && (cell.flags & terrain::kHorizonSea) == 0) {
                        ++counts.inland;
                        counts.inlandWater +=
                            blend.materials[0] == terrain::SurfaceMaterial::Water ? 1 : 0;
                    }
                }
            }
        }
    }
}

} // namespace

int main() {
    Counts total;
    for (const Place& place : kPlaces) {
        Counts counts;
        comparePlace(place, counts);
        std::printf("%-22s %4d texels, %4d = level 0, %4d dry below 0 m (%d ocean), %4d under a "
                    "lake or a river (%d water)\n",
                    place.name, counts.texels, counts.same, counts.dryBelow, counts.dryBelowOcean,
                    counts.inland, counts.inlandWater);
        total.texels += counts.texels;
        total.same += counts.same;
        total.dryBelow += counts.dryBelow;
        total.dryBelowOcean += counts.dryBelowOcean;
        total.inland += counts.inland;
        total.inlandWater += counts.inlandWater;
    }
    TEST_CHECK(total.same == total.texels);
    // The places hold both cases, and the tiles read them as the core does.
    TEST_CHECK(total.dryBelow > 0 && total.dryBelowOcean == 0);
    TEST_CHECK(total.inland > 0 && total.inlandWater == total.inland);
    return test::result();
}
