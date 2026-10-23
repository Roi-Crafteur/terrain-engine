#include "storage_codec.hpp"

#include "terrain/generation/vegetation.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace terrain::storage {

namespace {

// The point (x, z) stands in the chunk: x in [16 cx, 16 cx + 16), z likewise. Compared in
// meters, never through a chunk index, which a huge coordinate would overflow.
bool inChunk(double x, double z, ChunkCoord coord) {
    const double x0 = coord.x * kChunkSizeMeters;
    const double z0 = coord.z * kChunkSizeMeters;
    return x >= x0 && x < x0 + kChunkSizeMeters && z >= z0 && z < z0 + kChunkSizeMeters;
}

void readBlocks(ByteReader& in, ChunkCoord coord, ModificationStore& store) {
    const std::size_t count = in.count(kBlockRecord);
    std::optional<BlockCoord> previous;
    for (std::size_t i = 0; i < count; ++i) {
        BlockCoord block;
        block.x = in.i32();
        block.y = in.i32();
        block.z = in.i32();
        const std::uint8_t edit = in.u8();
        if (edit > static_cast<std::uint8_t>(BlockEdit::Removed)) {
            in.fail("unknown block edit");
        }
        if (blockChunk(block) != coord) {
            in.fail("misplaced or repeated block");
        }
        if (previous && !(*previous < block)) {
            in.fail("blocks out of order or repeated");
        }
        previous = block;
        store.setBlock(block, static_cast<BlockEdit>(edit));
    }
}

void readRemovedPlants(ByteReader& in, ChunkCoord coord, ModificationStore& store) {
    const std::size_t count = in.count(kSpotRecord);
    std::optional<PlantSpot> previous;
    for (std::size_t i = 0; i < count; ++i) {
        PlantSpot spot;
        spot.x = in.f64();
        spot.z = in.f64();
        if (!inChunk(spot.x, spot.z, coord)) {
            in.fail("misplaced or repeated removed plant");
        }
        if (previous && !(*previous < spot)) {
            in.fail("removed plants out of order or repeated");
        }
        previous = spot;
        store.removeGeneratedPlant(spot);
    }
}

void readAddedPlants(ByteReader& in, ChunkCoord coord, ModificationStore& store) {
    const std::size_t count = in.count(kPlantRecord);
    for (std::size_t i = 0; i < count; ++i) {
        Plant plant;
        const std::uint8_t type = in.u8();
        if (type >= kPlantTypeCount) {
            in.fail("unknown plant type");
        }
        plant.type = static_cast<PlantType>(type);
        plant.x = in.f64();
        plant.z = in.f64();
        plant.y = in.f32();
        plant.scale = in.f32();
        plant.rotation = in.f32();
        if (!inChunk(plant.x, plant.z, coord)) {
            in.fail("misplaced added plant");
        }
        store.addPlant(plant);
    }
}

} // namespace

std::size_t chunkRecordSize(const ChunkModifications& chunk) {
    return kMinChunkRecord + chunk.blockCount() * kBlockRecord +
           chunk.removedPlants.size() * kSpotRecord + chunk.addedPlants.size() * kPlantRecord;
}

void writeChunk(ByteWriter& out, ChunkCoord coord, const ChunkModifications& chunk) {
    out.i32(coord.x);
    out.i32(coord.z);
    const std::vector<std::pair<BlockCoord, BlockEdit>> blocks = chunk.blocks(coord);
    out.count(blocks.size());
    for (const auto& [block, edit] : blocks) {
        out.i32(block.x);
        out.i32(block.y);
        out.i32(block.z);
        out.u8(static_cast<std::uint8_t>(edit));
    }
    out.count(chunk.removedPlants.size());
    for (const PlantSpot& spot : chunk.removedPlants) {
        out.f64(spot.x);
        out.f64(spot.z);
    }
    out.count(chunk.addedPlants.size());
    for (const Plant& plant : chunk.addedPlants) {
        out.u8(static_cast<std::uint8_t>(plant.type));
        out.f64(plant.x);
        out.f64(plant.z);
        out.f32(plant.y);
        out.f32(plant.scale);
        out.f32(plant.rotation);
    }
}

ChunkCoord readChunkCoord(ByteReader& in) {
    ChunkCoord coord;
    coord.x = in.i32();
    coord.z = in.i32();
    return coord;
}

void readChunkBody(ByteReader& in, ChunkCoord coord, ModificationStore& store) {
    readBlocks(in, coord, store);
    readRemovedPlants(in, coord, store);
    readAddedPlants(in, coord, store);
    if (store.chunk(coord) == nullptr) {
        in.fail("empty chunk");
    }
}

ByteWriter beginFrame(const Magic& magic, std::uint32_t version, std::size_t bodySize) {
    ByteWriter out(kFrameSize + bodySize);
    out.bytes(magic);
    out.u32(version);
    out.u64(0); // the size of the file, written by sealFrame
    return out;
}

std::vector<std::uint8_t> sealFrame(ByteWriter out) {
    out.patchU64(8, out.size() + 4);
    out.u32(fnv1a(out.written()));
    return out.take();
}

Frame openFrame(std::span<const std::uint8_t> bytes, const Magic& magic, std::uint32_t current,
                const std::string& what) {
    const auto error = [&what](StorageErrorKind kind, const std::string& message) {
        return StorageError(kind, what + ": " + message);
    };
    if (bytes.size() < kFrameSize) {
        throw error(StorageErrorKind::Truncated,
                    "truncated (" + std::to_string(bytes.size()) + " bytes)");
    }
    const bool ours = std::equal(magic.begin(), magic.end(), bytes.begin());
    ByteReader header(bytes.subspan(4, kFrameHeaderSize - 4), what + ": ");
    const std::uint32_t version = header.u32();
    const std::uint64_t size = header.u64();
    ByteReader trailer(bytes.last(4), what + ": ");
    const auto body = bytes.first(bytes.size() - 4);
    std::optional<StorageError> damage;
    if (size > bytes.size()) {
        damage = error(StorageErrorKind::Truncated, "truncated: " + std::to_string(bytes.size()) +
                                                        " of " + std::to_string(size) + " bytes");
    } else if (size < bytes.size()) {
        damage = error(StorageErrorKind::Corrupt, "longer than its header announces: damaged");
    } else if (trailer.u32() != fnv1a(body)) {
        damage = error(StorageErrorKind::Corrupt, "checksum mismatch: the file is damaged");
    }
    if (damage) {
        throw ours ? *damage
                   : error(StorageErrorKind::Corrupt, "not a " + what + " file, or a damaged one");
    }
    if (!ours) {
        throw error(StorageErrorKind::Invalid, "not a " + what + " file");
    }
    if (version == 0) {
        throw error(StorageErrorKind::Invalid, "unknown format version 0");
    }
    if (version > current) {
        throw error(StorageErrorKind::NewerVersion,
                    "made by a newer version of the game (format " + std::to_string(version) +
                        ", this one reads up to " + std::to_string(current) +
                        "): update the game to open it");
    }
    return {version, body.subspan(kFrameHeaderSize)};
}

} // namespace terrain::storage
