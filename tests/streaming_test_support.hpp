#pragma once

// Helpers shared by the tests of the LOD, the streaming and the modifications.

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace streaming_test {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// Center of the searches of the tests: the origin is on the equator, warm everywhere; at about
// 44 degrees north the climate varies from cold to mild, as the tests need. This center lies
// inland there, in humid forests and taiga with some snow (3 degrees on average around it,
// hundreds of cave entrances). A multiple of 64 chunks (1024 m), as the lattices below.
inline constexpr std::int32_t kCenterChunkX = -77'824;
inline constexpr std::int32_t kCenterChunkZ = -531'264;
inline constexpr double kCenterX = kCenterChunkX * terrain::kChunkSizeMeters; // -1,245,184 m
inline constexpr double kCenterZ = kCenterChunkZ * terrain::kChunkSizeMeters; // -8,500,224 m

// Small LOD rings for fast tests: LOD 0 within 2 chunks, LOD 3 beyond 8, a view of 10 chunks.
inline terrain::LodParams smallLod() {
    return {{2, 4, 8}, 10};
}

// First chunk of a lattice of 1024 m (64 chunks) around the center whose mountain layer rises
// above 1000 m: steep, curved ground.
inline terrain::ChunkCoord mountainChunk(const terrain::Heightfield& field) {
    for (int j = -40; j <= 40; ++j) {
        for (int i = -40; i <= 40; ++i) {
            if (field.sampleAt(kCenterX + i * 1024.0, kCenterZ + j * 1024.0).mountain > 1000.0f) {
                return {kCenterChunkX + i * 64, kCenterChunkZ + j * 64};
            }
        }
    }
    return {kCenterChunkX, kCenterChunkZ};
}

// First chunk of a lattice of 512 m (32 chunks) around the center, on land, whose LOD 0 tile
// holds at least minPlants plants.
inline terrain::ChunkCoord plantChunk(const terrain::TerrainTiles& tiles, std::size_t minPlants) {
    for (int j = -20; j <= 20; ++j) {
        for (int i = -20; i <= 20; ++i) {
            const terrain::ChunkCoord chunk{kCenterChunkX + i * 32, kCenterChunkZ + j * 32};
            const double x = chunk.x * terrain::kChunkSizeMeters;
            const double z = chunk.z * terrain::kChunkSizeMeters;
            if (tiles.heightfield().heightAt(x, z) < 20.0f) {
                continue;
            }
            if (tiles.generate({{chunk, 0}, 0}).plants.size() >= minPlants) {
                return chunk;
            }
        }
    }
    return {kCenterChunkX, kCenterChunkZ};
}

// Cave entrances of the world with caves around the center, within reach of 4096 m of it on
// both axes, in cell order: found from the entrance lattice, never by generating tiles.
inline std::vector<terrain::CaveEntrance> entrancesNearOrigin(const terrain::TerrainTiles& tiles) {
    return tiles.caves()->entrancesNear(kCenterX - 4096.0, kCenterZ - 4096.0, kCenterX + 4096.0,
                                        kCenterZ + 4096.0, tiles.caveGround());
}

// Chunk of the opening of an entrance.
inline terrain::ChunkCoord entranceChunk(const terrain::CaveEntrance& entrance) {
    return terrain::worldToChunk(entrance.topX, entrance.topZ);
}

struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline WorldPoint worldVertex(const terrain::TileData& data, std::size_t index) {
    const terrain::Vec3f v = data.mesh.vertices[index];
    return {data.originX + v.x, data.originY + v.y, data.originZ + v.z};
}

} // namespace streaming_test
