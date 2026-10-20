#pragma once

// Helpers of the cave material tests: the environment of the default world, the tiles around the
// entrances, the paint of their vertices, and the lake of a tile with its checks.

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "streaming_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace cave_materials_test {

using terrain::CaveEntrance;
using terrain::CaveEnvironment;
using terrain::CaveEnvironmentField;
using terrain::CaveMaterial;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;

constexpr std::uint64_t kSeed = streaming_test::kSeed;

// A wall that is plain stone: shallow-ish, a wall, away from any lake, in a mild dry climate.
inline terrain::CaveWallSite stoneWall() {
    terrain::CaveWallSite site;
    site.environment = caves_test::environmentOf(terrain::Biome::Grassland, 12.0f, 0.3f);
    site.depth = 40.0f;
    site.normalY = 0.0f;
    return site;
}

inline terrain::CaveWallSite withBiome(terrain::CaveWallSite site, terrain::Biome biome,
                                       float temperature, float humidity) {
    site.environment = caves_test::environmentOf(biome, temperature, humidity);
    return site;
}

struct World {
    terrain::Heightfield heightfield;
    terrain::Ecology ecology;
    CaveEnvironmentField field;

    explicit World(std::uint64_t seed)
        : heightfield(seed), ecology(seed),
          field(heightfield, ecology.climate(), ecology.params().biomes) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;
};

struct Slope {
    double temperature = 0.0;
    double humidity = 0.0;

    void widen(const CaveEnvironment& from, const CaveEnvironment& to) {
        temperature =
            std::max(temperature, static_cast<double>(std::abs(to.temperature - from.temperature)));
        humidity = std::max(humidity, static_cast<double>(std::abs(to.humidity - from.humidity)));
    }
};

// Largest difference between nodes along x (alongX) or along z around the cell (i, k): the
// gradients of the cells (i, k), (i + 1, k), (i, k + 1) and (i + 1, k + 1) are bounded by it.
inline Slope slopeAround(const CaveEnvironmentField& field, std::int64_t i, std::int64_t k,
                         bool alongX) {
    Slope slope;
    for (std::int64_t a = 0; a <= 1; ++a) {
        for (std::int64_t b = 0; b <= 1; ++b) {
            const CaveEnvironment from = field.node(i + a, k + b);
            slope.widen(from, alongX ? field.node(i + a + 1, k + b) : field.node(i + a, k + b + 1));
        }
    }
    return slope;
}

// Chunks around the first entrances: the opening and the chamber of the first, the opening of
// the second.
inline std::vector<ChunkCoord> entranceChunks(const std::vector<CaveEntrance>& entrances) {
    return {streaming_test::entranceChunk(entrances[0]),
            terrain::worldToChunk(entrances[0].bottomX, entrances[0].bottomZ),
            streaming_test::entranceChunk(entrances[1])};
}

inline terrain::TileRequest requestOf(ChunkCoord chunk, int lod) {
    return {terrain::tileContaining(chunk, lod), 0};
}

struct SurfaceCounts {
    std::size_t vertices = 0;
    std::size_t outer = 0; // within half a meter of the ground (or over it)
    std::size_t outerPainted = 0;
    std::size_t deep = 0;        // at least 3 m under the ground
    std::size_t deepPainted = 0; // of them painted as the outer surface
};

inline SurfaceCounts countSurface(const TerrainTiles& tiles, const TileData& data) {
    SurfaceCounts counts;
    for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, n);
        const double depth = static_cast<double>(tiles.heightfield().heightAt(p.x, p.z)) - p.y;
        const bool surface = data.materials[n] == CaveMaterial::Surface;
        ++counts.vertices;
        counts.outer += depth < 0.5 ? 1 : 0;
        counts.outerPainted += depth < 0.5 && surface ? 1 : 0;
        counts.deep += depth >= 3.0 ? 1 : 0;
        counts.deepPainted += depth >= 3.0 && surface ? 1 : 0;
    }
    return counts;
}

inline bool onlyWaterOrIce(const TileData& data) {
    return std::all_of(data.waterMaterials.begin(), data.waterMaterials.end(), [](CaveMaterial m) {
        return m == CaveMaterial::Water || m == CaveMaterial::Ice;
    });
}

struct Lake {
    ChunkCoord chunk;
    std::size_t samples = 0;
};

// A chunk holding water on its lattice: the wet land cells around the origin are scanned one
// after the other, every 4 m horizontally and 2 m vertically under their level, and the first
// cell holding enough water gives the chunk with the most. At most kCells cells are scanned.
inline std::optional<Lake> findLake(const TerrainTiles& tiles) {
    constexpr int kCells = 30;
    constexpr int kRadius = 64;
    const terrain::CaveField& field = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    terrain::AquiferCache cache(field, ground);
    int scanned = 0;
    for (int cellZ = -6; cellZ < 6 && scanned < kCells; ++cellZ) {
        for (int cellX = -6; cellX < 6 && scanned < kCells; ++cellX) {
            const terrain::AquiferCell cell = cache.at(cellX, cellZ);
            if (!cell.wet || ground.height(cell.siteX, cell.siteZ) < 2.0f) {
                continue;
            }
            ++scanned;
            const int x0 = static_cast<int>(std::lround(cell.siteX / 4.0)) * 4;
            const int z0 = static_cast<int>(std::lround(cell.siteZ / 4.0)) * 4;
            const int top = static_cast<int>(std::floor(cell.level));
            terrain::CaveSampler sampler(field, ground, terrain::caveDetailForLod(0),
                                         {x0 - kRadius - 1.0, top - 26.0, z0 - kRadius - 1.0,
                                          x0 + kRadius + 1.0, top + 2.0, z0 + kRadius + 1.0});
            std::map<std::pair<int, int>, std::size_t> perChunk;
            for (int z = z0 - kRadius; z <= z0 + kRadius; z += 4) {
                for (int x = x0 - kRadius; x <= x0 + kRadius; x += 4) {
                    const terrain::CaveColumn column = sampler.column(x, z, ground.height(x, z));
                    for (int y = top - 1; y >= top - 25 && column.waterLevel > terrain::kDryLevel;
                         y -= 2) {
                        const auto solid =
                            static_cast<float>(y - static_cast<double>(column.ground));
                        const float carved = sampler.density(column, y, solid);
                        if (sampler.waterDensity(column, y, solid, carved) < 0.0f) {
                            const ChunkCoord chunk = terrain::worldToChunk(x, z);
                            ++perChunk[{chunk.x, chunk.z}];
                        }
                    }
                }
            }
            const auto best =
                std::max_element(perChunk.begin(), perChunk.end(),
                                 [](const auto& a, const auto& b) { return a.second < b.second; });
            if (best != perChunk.end() && best->second >= 3) {
                return Lake{{best->first.first, best->first.second}, best->second};
            }
        }
    }
    return std::nullopt;
}

struct LakeChecks {
    std::size_t vertices = 0;
    std::size_t belowGround = 0;
    std::size_t inAir = 0;      // carved density >= -1 within a cell of the vertex
    std::size_t sameCell = 0;   // the nearest aquifer site is wet
    std::size_t iceMatches = 0; // ice exactly over frozen cells
};

inline LakeChecks checkLake(const TerrainTiles& tiles, const TileData& data) {
    const terrain::CaveGround& ground = tiles.caveGround();
    double top = data.originY;
    for (const terrain::Vec3f& v : data.water.vertices) {
        top = std::max(top, data.originY + v.y);
    }
    const double side = terrain::tileSizeMeters(0);
    terrain::CaveSampler sampler(*tiles.caves(), ground, terrain::caveDetailForLod(0),
                                 {data.originX - 2.0, data.originY - 2.0, data.originZ - 2.0,
                                  data.originX + side + 2.0, top + 2.0, data.originZ + side + 2.0});
    terrain::AquiferCache cache(*tiles.caves(), ground);
    LakeChecks checks;
    for (std::size_t n = 0; n < data.water.vertices.size(); ++n) {
        const terrain::Vec3f v = data.water.vertices[n];
        const double x = data.originX + v.x;
        const double y = data.originY + v.y;
        const double z = data.originZ + v.z;
        const float height = tiles.heightfield().heightAt(x, z);
        const terrain::CaveColumn column = sampler.column(x, z, height);
        const float carved =
            sampler.density(column, y, static_cast<float>(y - static_cast<double>(height)));
        const terrain::AquiferCell& cell = cache.nearest(x, z);
        ++checks.vertices;
        checks.belowGround += y < static_cast<double>(height) ? 1 : 0;
        checks.inAir += carved >= -1.0f ? 1 : 0;
        checks.sameCell += cell.wet ? 1 : 0;
        checks.iceMatches +=
            data.waterMaterials[n] == (cell.frozen ? CaveMaterial::Ice : CaveMaterial::Water) ? 1
                                                                                              : 0;
    }
    return checks;
}

// Speleothems of a LOD 0 tile: those of the placement over its square, from a sampler of the
// carved detail of the tile.
inline bool speleothemsMatch(const TerrainTiles& tiles, const TileData& data) {
    const double side = terrain::tileSizeMeters(0);
    const double depth = static_cast<double>(tiles.caves()->params().maxDepth) + 2.0;
    terrain::CaveSampler sampler(*tiles.caves(), tiles.caveGround(), terrain::caveDetailForLod(0),
                                 {data.originX, static_cast<double>(data.minHeight) - depth - 4.0,
                                  data.originZ, data.originX + side,
                                  static_cast<double>(data.maxHeight) + 4.0, data.originZ + side});
    const terrain::SpeleothemField field(kSeed, tiles.caves()->params().speleothems);
    const bool placed = data.speleothems == field.place(sampler, data.originX, data.originZ,
                                                        data.originX + side, data.originZ + side);
    return placed && std::all_of(data.speleothems.begin(), data.speleothems.end(),
                                 [&](const terrain::Speleothem& s) {
                                     return s.x >= data.originX && s.x < data.originX + side &&
                                            s.z >= data.originZ && s.z < data.originZ + side;
                                 });
}

} // namespace cave_materials_test
