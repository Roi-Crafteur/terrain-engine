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

// Small LOD rings for fast tests: LOD 0 within 2 chunks, LOD 3 beyond 8, a view of 10 chunks.
inline terrain::LodParams smallLod() {
    return {{2, 4, 8}, 10};
}

// First chunk of a lattice of 1024 m (64 chunks) around the origin whose mountain layer rises
// above 1000 m: steep, curved ground.
inline terrain::ChunkCoord mountainChunk(const terrain::Heightfield& field) {
    for (int j = -40; j <= 40; ++j) {
        for (int i = -40; i <= 40; ++i) {
            if (field.sampleAt(i * 1024.0, j * 1024.0).mountain > 1000.0f) {
                return {i * 64, j * 64};
            }
        }
    }
    return {};
}

// First chunk of a lattice of 512 m (32 chunks) around the origin, on land, whose LOD 0 tile
// holds at least minPlants plants.
inline terrain::ChunkCoord plantChunk(const terrain::TerrainTiles& tiles, std::size_t minPlants) {
    for (int j = -20; j <= 20; ++j) {
        for (int i = -20; i <= 20; ++i) {
            const terrain::ChunkCoord chunk{i * 32, j * 32};
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
    return {};
}

// Cave entrances of the world with caves around the origin, within reach of [-4096, 4096] on
// both axes, in cell order: found from the entrance lattice, never by generating tiles.
inline std::vector<terrain::CaveEntrance> entrancesNearOrigin(const terrain::TerrainTiles& tiles) {
    return tiles.caves()->entrancesNear(-4096.0, -4096.0, 4096.0, 4096.0, tiles.caveGround());
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
