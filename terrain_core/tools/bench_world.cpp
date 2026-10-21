// The tiles, the parts of a LOD 0 tile and the height grids of terrain_bench_noise, on the
// default world and on the planet: see bench_noise.cpp.

#include "bench_world.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace bench {

namespace {

using terrain::Heightfield;

// First chunk of a lattice of 1024 m (64 chunks) around the center of the tests, then around
// the origin, whose sample the predicate accepts; the center otherwise. Around the center
// first, as streaming_test::mountainChunk.
template <class Accept>
terrain::ChunkCoord chunkWhere(const Heightfield& field, Accept&& accept) {
    const std::array<terrain::ChunkCoord, 2> centers = {{{kCenterChunkX, kCenterChunkZ}, {0, 0}}};
    for (const terrain::ChunkCoord& center : centers) {
        for (int j = -40; j <= 40; ++j) {
            for (int i = -40; i <= 40; ++i) {
                const terrain::ChunkCoord chunk{center.x + i * 64, center.z + j * 64};
                if (accept(field.sampleAt(chunk.x * terrain::kChunkSizeMeters,
                                          chunk.z * terrain::kChunkSizeMeters))) {
                    return chunk;
                }
            }
        }
    }
    return centers[0];
}

bool mountainous(const terrain::HeightSample& s) {
    return s.mountain > 1000.0f;
}

bool plain(const terrain::HeightSample& s) {
    return s.height > 20.0f && s.height < 400.0f && s.mountain == 0.0f;
}

// side x side adjacent tiles of the LOD from the tile holding the chunk.
std::vector<terrain::TileKey> tileBlock(terrain::ChunkCoord chunk, int lod, int side) {
    const terrain::TileKey first = terrain::tileContaining(chunk, lod);
    const int step = terrain::tileChunks(lod);
    std::vector<terrain::TileKey> keys;
    for (int k = 0; k < side; ++k) {
        for (int i = 0; i < side; ++i) {
            keys.push_back({{first.origin.x + i * step, first.origin.z + k * step}, lod});
        }
    }
    return keys;
}

Timing generateTiles(const Config& c, const terrain::TerrainTiles& tiles,
                            const std::vector<terrain::TileKey>& keys) {
    return measure(c.runs, [&tiles, &keys] {
        Checksum sum;
        for (const terrain::TileKey& key : keys) {
            sum.add(tiles.generate({key, 0}));
        }
        return sum.value();
    });
}

// Tiles of LOD 0 to 3 around a mountain chunk and a plain chunk, and cave tiles of LOD 0 and 1
// around the plain chunk.
void tileCases(const Config& c, const std::string& suffix, const terrain::HeightfieldParams& params,
               terrain::ChunkCoord mountain, terrain::ChunkCoord plainChunk) {
    const terrain::TerrainTiles tiles(kSeed, params);
    const int side = c.divisor == 1 ? 4 : 2;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        std::vector<terrain::TileKey> keys = tileBlock(mountain, lod, side);
        const std::vector<terrain::TileKey> plains = tileBlock(plainChunk, lod, side);
        keys.insert(keys.end(), plains.begin(), plains.end());
        const std::string name = "tile LOD " + std::to_string(lod) + suffix;
        record(name.c_str(), generateTiles(c, tiles, keys), keys.size(), "ms");
    }
    const terrain::TerrainTiles caveTiles(kSeed, params, {}, terrain::CaveParams{});
    for (int lod = 0; lod < 2; ++lod) {
        const std::vector<terrain::TileKey> keys = tileBlock(plainChunk, lod, side);
        const std::string name = "cave tile LOD " + std::to_string(lod) + suffix;
        record(name.c_str(), generateTiles(c, caveTiles, keys), keys.size(), "ms");
    }
}

// The 17 x 17 columns of LOD 0 tiles, point by point and in one batch per tile.
void columnParts(const Config& c, const Heightfield& field,
                 const std::vector<terrain::TileKey>& keys) {
    const auto columns = measure(c.runs, [&field, &keys] {
        Checksum sum;
        for (const terrain::TileKey& key : keys) {
            for (int k = 0; k < terrain::kGridSamples; ++k) {
                for (int i = 0; i < terrain::kGridSamples; ++i) {
                    sum.add(field.heightAt(key.origin.x * terrain::kChunkCells + i,
                                           key.origin.z * terrain::kChunkCells + k));
                }
            }
        }
        return sum.value();
    });
    record("columns, 17 x 17 heightAt", columns, keys.size(), "ms");
    const auto batch = measure(c.runs, [&field, &keys] {
        Checksum sum;
        std::vector<double> xs;
        std::vector<double> zs;
        std::vector<float> heights(static_cast<std::size_t>(terrain::kGridSamples) *
                                   static_cast<std::size_t>(terrain::kGridSamples));
        for (const terrain::TileKey& key : keys) {
            xs.clear();
            zs.clear();
            for (int k = 0; k < terrain::kGridSamples; ++k) {
                for (int i = 0; i < terrain::kGridSamples; ++i) {
                    xs.push_back(key.origin.x * terrain::kChunkCells + i);
                    zs.push_back(key.origin.z * terrain::kChunkCells + k);
                }
            }
            field.heightsAt(xs, zs, heights);
            for (const float h : heights) {
                sum.add(h);
            }
        }
        return sum.value();
    });
    report("columns, 17 x 17 heightsAt (batch)", batch, keys.size(), "ms");
}

// The work of a LOD 0 tile that reads the relief: its 17 x 17 columns, the map of its plants
// (relief, climate, biomes and densities every 4 m) and their placement.
void lod0Parts(const Config& c, const Heightfield& field, terrain::ChunkCoord mountain) {
    section("parts of a LOD 0 tile, ms per tile");
    const terrain::Ecology ecology(kSeed);
    const int side = c.divisor == 1 ? 4 : 2;
    std::vector<terrain::TileKey> keys = tileBlock(mountain, 0, side);
    const std::vector<terrain::TileKey> plain = tileBlock({kCenterChunkX, kCenterChunkZ}, 0, side);
    keys.insert(keys.end(), plain.begin(), plain.end());
    columnParts(c, field, keys);
    const auto regionOf = [](const terrain::TileKey& key) {
        const double x0 = key.origin.x * terrain::kChunkSizeMeters;
        const double z0 = key.origin.z * terrain::kChunkSizeMeters;
        return terrain::HeightmapRegion{x0 + 8.0, z0 + 8.0, 4.0, 6};
    };
    const auto surfaces = measure(c.runs, [&] {
        Checksum sum;
        for (const terrain::TileKey& key : keys) {
            const terrain::TerrainSurface s = terrain::surfaceFromHeightfield(field, regionOf(key));
            for (const terrain::SurfaceSite& site : s.sites) {
                sum.add(site.ground);
            }
        }
        return sum.value();
    });
    record("plant map: surfaceFromHeightfield", surfaces, keys.size(), "ms");
    std::vector<terrain::TerrainSurface> built;
    for (const terrain::TileKey& key : keys) {
        built.push_back(terrain::surfaceFromHeightfield(field, regionOf(key)));
    }
    const auto maps = measure(c.runs, [&ecology, &built] {
        Checksum sum;
        for (const terrain::TerrainSurface& s : built) {
            for (const terrain::EcologySample& e : ecology.map(s).samples) {
                sum.add(e.climate.temperature);
            }
        }
        return sum.value();
    });
    record("plant map: Ecology::map (climate...)", maps, keys.size(), "ms");
    const terrain::ReliefHeight relief = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    const auto placement = measure(c.runs, [&] {
        Checksum sum;
        for (std::size_t t = 0; t < keys.size(); ++t) {
            const double x0 = keys[t].origin.x * terrain::kChunkSizeMeters;
            const double z0 = keys[t].origin.z * terrain::kChunkSizeMeters;
            const terrain::EcologyMap map = ecology.map(built[t]);
            const terrain::PlantArea area{x0, z0, x0 + terrain::kChunkSizeMeters,
                                          z0 + terrain::kChunkSizeMeters};
            for (const terrain::Plant& plant : ecology.placePlants(map, relief, area)) {
                sum.add(plant.y);
            }
        }
        return sum.value();
    });
    record("plant map + placement (heightAt)", placement, keys.size(), "ms");
}

void gridCase(const Config& c, const char* name, const Heightfield& field,
              const terrain::HeightmapRegion& region) {
    const auto timing = measure(std::min(c.runs, 3), [&field, &region] {
        Checksum sum;
        for (const float h : terrain::sampleHeightGrid(field, region).heights) {
            sum.add(h);
        }
        return sum.value();
    });
    record(name, timing, 1, "ms");
    const auto side = static_cast<std::size_t>(region.size);
    const std::size_t points = side * side;
    report("  per point", timing, points, "ns");
}

} // namespace

void worldCases(const Config& c, const Heightfield& field, const Heightfield& planet) {
    const terrain::ChunkCoord mountain = chunkWhere(field, mountainous);
    section("tiles of the default world (mountains and inland plains), ms per tile");
    tileCases(c, "", field.params(), mountain, {kCenterChunkX, kCenterChunkZ});
    section("tiles of the planet (mountains and plains), ms per tile");
    tileCases(c, ", planet", planet.params(), chunkWhere(planet, mountainous),
              chunkWhere(planet, plain));
    lod0Parts(c, field, mountain);
    const int size = c.divisor == 1 ? 1024 : 256;
    std::printf("\n== height grids of %d x %d, ms per grid\n", size, size);
    gridCase(c, "grid 100 m/px (default region)", field, {0.0, 0.0, 100.0, size});
    gridCase(c, "grid 1 m/px (mountains)", field,
             {mountain.x * terrain::kChunkSizeMeters, mountain.z * terrain::kChunkSizeMeters, 1.0,
              size});
    gridCase(c, "grid planet 100 m/px", planet, {0.0, 0.0, 100.0, size});
    gridCase(c, "grid planet 8 km/px (macro, plates)", planet, {0.0, 0.0, 8000.0, size});
}

} // namespace bench
