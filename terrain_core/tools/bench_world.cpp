// The tiles, their meshing, the parts of a LOD 0 tile and the height grids of
// terrain_bench_noise, on the default world and on the planet: see bench_noise.cpp.

#include "bench_world.hpp"
#include "tile_blend.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bench {

namespace {

using terrain::Heightfield;

// First chunk of a lattice of 1024 m (64 chunks) around the center of the tests, then around
// the origin, whose sample the predicate accepts; then the same on a lattice of 16,384 m; the
// center otherwise. Around the center first, as streaming_test::mountainChunk.
template <class Accept>
terrain::ChunkCoord chunkWhere(const Heightfield& field, Accept&& accept) {
    const std::array<terrain::ChunkCoord, 2> centers = {{{kCenterChunkX, kCenterChunkZ}, {0, 0}}};
    for (const int spacing : {64, 1024}) {
        for (const terrain::ChunkCoord& center : centers) {
            for (int j = -40; j <= 40; ++j) {
                for (int i = -40; i <= 40; ++i) {
                    const terrain::ChunkCoord chunk{center.x + i * spacing,
                                                    center.z + j * spacing};
                    if (accept(field.sampleAt(chunk.x * terrain::kChunkSizeMeters,
                                              chunk.z * terrain::kChunkSizeMeters))) {
                        return chunk;
                    }
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

// Tiles of LOD 0 to 3 around a mountain chunk and a plain chunk, timed apart, and cave tiles of
// LOD 0 and 1 around the plain chunk.
void tileCases(const Config& c, const std::string& suffix, const terrain::HeightfieldParams& params,
               terrain::ChunkCoord mountain, terrain::ChunkCoord plainChunk) {
    const terrain::TerrainTiles tiles(kSeed, params);
    const int side = c.divisor == 1 ? 4 : 2;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        for (const auto& [where, chunk] : {std::pair{", mountain", mountain},
                                           std::pair{", plain", plainChunk}}) {
            const std::vector<terrain::TileKey> keys = tileBlock(chunk, lod, side);
            const std::string name = "tile LOD " + std::to_string(lod) + where + suffix;
            record(name.c_str(), generateTiles(c, tiles, keys), keys.size(), "ms");
        }
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

// The surface blend of the tiles (TileData::surfaceBlend, tile_blend.cpp): their texels every 4
// cells (5 x 5, those of the tiles) and every 8 (3 x 3), with the heights beyond their sides.
// Reported apart from the total checksum, which stays that of the world before the blend.
void blendParts(const Config& c, const Heightfield& field, const terrain::Ecology& ecology,
                const std::vector<terrain::TileKey>& keys) {
    std::vector<terrain::tile::TileLattice> lattices;
    std::vector<terrain::tile::TileColumns> columns;
    for (const terrain::TileKey& key : keys) {
        lattices.push_back(terrain::tile::latticeOf(key));
        columns.push_back(terrain::tile::sampleColumns(field, lattices.back()));
    }
    for (const int stride : {4, 8}) {
        const auto texels = measure(c.runs, [&, stride] {
            Checksum sum;
            for (std::size_t t = 0; t < keys.size(); ++t) {
                for (const terrain::BlendTexel& texel : terrain::tile::blendTexels(
                         field, ecology, lattices[t], columns[t], stride)) {
                    sum.add(static_cast<std::uint64_t>(texel.ids) << 32 | texel.weights);
                }
            }
            return sum.value();
        });
        report(stride == 4 ? "surface blend: 5 x 5 texels (the tiles)"
                           : "surface blend: 3 x 3 texels",
               texels, keys.size(), "ms");
    }
}

// The work of a LOD 0 tile that reads the relief: its 17 x 17 columns, the map of its plants
// (relief, climate, biomes and densities every 4 m), their placement and the surface blend.
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
    blendParts(c, field, ecology, keys);
}

// Ground heights of the 17 x 17 columns of a tile.
std::vector<float> tileColumns(const Heightfield& field, const terrain::TileKey& key) {
    const auto spacing = static_cast<int>(terrain::tileCellMeters(key.lod));
    std::vector<double> xs;
    std::vector<double> zs;
    for (int k = 0; k < terrain::kGridSamples; ++k) {
        for (int i = 0; i < terrain::kGridSamples; ++i) {
            xs.push_back(key.origin.x * terrain::kChunkCells + i * spacing);
            zs.push_back(key.origin.z * terrain::kChunkCells + k * spacing);
        }
    }
    std::vector<float> heights(xs.size());
    field.heightsAt(xs, zs, heights);
    return heights;
}

// The density grid of the level of a tile whose lowest samples lie at originY, from the heights
// of its columns: y - height, as the tiles compute it; carved by the caves of the detail of the
// LOD if carved.
terrain::DensityGrid levelGrid(const terrain::TerrainTiles& tiles, const terrain::TileKey& key,
                               const std::vector<float>& columns, int originY, bool carved) {
    const double spacing = terrain::tileCellMeters(key.lod);
    const double side = terrain::tileSizeMeters(key.lod);
    const double x0 = key.origin.x * terrain::kChunkSizeMeters;
    const double y0 = originY;
    const double z0 = key.origin.z * terrain::kChunkSizeMeters;
    std::optional<terrain::CaveSampler> sampler;
    if (carved) {
        sampler.emplace(*tiles.caves(), tiles.caveGround(), terrain::caveDetailForLod(key.lod),
                        terrain::CaveBox{x0, y0, z0, x0 + side, y0 + side, z0 + side});
    }
    terrain::DensityGrid grid(0.0f);
    for (int k = 0; k < terrain::kGridSamples; ++k) {
        for (int i = 0; i < terrain::kGridSamples; ++i) {
            const float h = columns[static_cast<std::size_t>(k * terrain::kGridSamples + i)];
            std::optional<terrain::CaveColumn> column;
            if (sampler) {
                column = sampler->column(x0 + i * spacing, z0 + k * spacing, h);
            }
            for (int j = 0; j < terrain::kGridSamples; ++j) {
                const double y = y0 + j * spacing;
                const auto density = static_cast<float>(y - h);
                grid.set(i, j, k, column ? sampler->density(*column, y, density) : density);
            }
        }
    }
    return grid;
}

bool crossesSurface(const terrain::DensityGrid& grid) {
    bool solid = false;
    bool air = false;
    for (int k = 0; k < terrain::kGridSamples; ++k) {
        for (int j = 0; j < terrain::kGridSamples; ++j) {
            for (int i = 0; i < terrain::kGridSamples; ++i) {
                (grid.at(i, j, k) < terrain::kIsoLevel ? solid : air) = true;
            }
        }
    }
    return solid && air;
}

// The density grids of the levels the tiles mesh: the levels meeting the heights of the
// columns; with caves (depth > 0), the levels reaching down to depth below the lowest column,
// carved, that cross the surface.
std::vector<terrain::DensityGrid> tileLevels(const terrain::TerrainTiles& tiles,
                                             const std::vector<terrain::TileKey>& keys,
                                             double depth) {
    std::vector<terrain::DensityGrid> grids;
    for (const terrain::TileKey& key : keys) {
        const auto height = static_cast<int>(terrain::tileSizeMeters(key.lod));
        const std::vector<float> columns = tileColumns(tiles.heightfield(), key);
        const auto [lowest, highest] = std::minmax_element(columns.begin(), columns.end());
        const double low = *lowest - depth;
        const double high = *highest;
        const auto first = static_cast<int>(std::ceil(low / height)) - 1;
        const auto last = static_cast<int>(std::floor(high / height));
        for (int level = first; level <= last; ++level) {
            terrain::DensityGrid grid = levelGrid(tiles, key, columns, level * height, depth > 0.0);
            if (depth == 0.0 || crossesSurface(grid)) {
                grids.push_back(std::move(grid));
            }
        }
    }
    return grids;
}

// marchingCubes over the levels of the tiles of LOD 0 to 3 around a mountain chunk and a plain
// chunk, and of the cave tiles of LOD 0 and 1 around the plain chunk (bench::tileCases).
void meshingCases(const Config& c, const std::string& suffix,
                  const terrain::HeightfieldParams& params, terrain::ChunkCoord mountain,
                  terrain::ChunkCoord plainChunk) {
    const auto run = [&c, &suffix](const std::string& name,
                                   const std::vector<terrain::DensityGrid>& grids) {
        const auto timing = measure(c.runs, [&grids] {
            Checksum sum;
            for (const terrain::DensityGrid& grid : grids) {
                const terrain::Mesh mesh = terrain::marchingCubes(grid);
                sum.add(mesh);
                for (const terrain::Vec3f& n : mesh.normals) {
                    sum.add(n.x);
                    sum.add(n.y);
                    sum.add(n.z);
                }
            }
            return sum.value();
        });
        record((name + suffix).c_str(), timing, grids.size(), "us");
    };
    const terrain::TerrainTiles tiles(kSeed, params);
    const int side = c.divisor == 1 ? 4 : 2;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        std::vector<terrain::TileKey> keys = tileBlock(mountain, lod, side);
        const std::vector<terrain::TileKey> plains = tileBlock(plainChunk, lod, side);
        keys.insert(keys.end(), plains.begin(), plains.end());
        run("marchingCubes, tile LOD " + std::to_string(lod), tileLevels(tiles, keys, 0.0));
    }
    const terrain::TerrainTiles caveTiles(kSeed, params, {}, terrain::CaveParams{});
    for (int lod = 0; lod < 2; ++lod) {
        run("marchingCubes, cave tile LOD " + std::to_string(lod),
            tileLevels(caveTiles, tileBlock(plainChunk, lod, side), 160.0));
    }
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
    const terrain::ChunkCoord fieldPlain = chunkWhere(field, plain);
    const terrain::ChunkCoord planetMountain = chunkWhere(planet, mountainous);
    const terrain::ChunkCoord planetPlain = chunkWhere(planet, plain);
    terrain::HeightfieldParams legacyParams;
    legacyParams.geography.enabled = false;
    const Heightfield legacy(kSeed, legacyParams);
    const terrain::ChunkCoord legacyMountain = chunkWhere(legacy, mountainous);
    const terrain::ChunkCoord legacyPlain = chunkWhere(legacy, plain);
    std::printf("\nmountain chunk (%d, %d), plain chunk (%d, %d) of the default world\n",
                mountain.x, mountain.z, fieldPlain.x, fieldPlain.z);
    section("tiles of the default world (mountains and inland plains), ms per tile");
    tileCases(c, "", field.params(), mountain, fieldPlain);
    section("tiles of the planet (mountains and plains), ms per tile");
    tileCases(c, ", planet", planet.params(), planetMountain, planetPlain);
    section("tiles of the legacy relief (generator 2, the world of main), ms per tile");
    tileCases(c, ", legacy", legacyParams, legacyMountain, legacyPlain);
    section("marchingCubes over the levels of the tiles, us per level");
    meshingCases(c, "", field.params(), mountain, fieldPlain);
    meshingCases(c, ", planet", planet.params(), planetMountain, planetPlain);
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
