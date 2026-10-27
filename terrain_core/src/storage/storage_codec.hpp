#pragma once

// Records shared by the modification files (TMOD) and the region files of format 1, and the
// frame of the world files. Shared by the files of src/storage only.

#include "byte_stream.hpp"
#include "terrain/chunk_coord.hpp"
#include "terrain/storage/modifications.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace terrain::storage {

// Record sizes in the files.
inline constexpr std::size_t kBlockRecord = 13;
inline constexpr std::size_t kSpotRecord = 16;
inline constexpr std::size_t kPlantRecord = 29;
// A chunk without any modification: its coordinates and three counts.
inline constexpr std::size_t kMinChunkRecord = 20;

[[nodiscard]] std::size_t chunkRecordSize(const ChunkModifications& chunk);

// Record of a chunk:
//   i32 x, i32 z,
//   u32 blocks, per block: i32 x, i32 y, i32 z, u8 edit,
//   u32 removed plants, per plant: f64 x, f64 z,
//   u32 added plants, per plant: u8 type, f64 x, f64 z, f32 y, f32 scale, f32 rotation.
// The blocks and the removed plants in increasing order, the added plants in order of addition.
void writeChunk(ByteWriter& out, ChunkCoord coord, const ChunkModifications& chunk);

// The plants of a chunk record, as above: u32 removed plants and their records, u32 added
// plants and their records.
void writePlants(ByteWriter& out, const ChunkModifications& chunk);
void readPlants(ByteReader& in, ChunkCoord coord, ModificationStore& store);

[[nodiscard]] ChunkCoord readChunkCoord(ByteReader& in);

// Reads the modifications of the chunk coord, which store does not hold yet. Rejects unknown
// edits and plant types, non-finite values, modifications outside the chunk, blocks or removed
// plants out of order or repeated, and a chunk without modifications.
void readChunkBody(ByteReader& in, ChunkCoord coord, ModificationStore& store);

using Magic = std::array<std::uint8_t, 4>;

// Frame of the world files, the same for every version so that a newer file is always told
// apart from a damaged one:
//   magic, u32 version, u64 size of the whole file, body, u32 FNV-1a of every byte before it.
inline constexpr std::size_t kFrameHeaderSize = 16;
inline constexpr std::size_t kFrameSize = kFrameHeaderSize + 4;

// A writer holding the header of a frame; the body follows, then sealFrame.
[[nodiscard]] ByteWriter beginFrame(const Magic& magic, std::uint32_t version,
                                    std::size_t bodySize);
// Writes the size of the file into its header and appends the checksum.
[[nodiscard]] std::vector<std::uint8_t> sealFrame(ByteWriter out);

struct Frame {
    std::uint32_t version = 0;
    std::span<const std::uint8_t> body;
};

// The first kFrameHeaderSize bytes of a frame: its magic is magic (ours), its version and the
// size it announces.
struct FrameHeader {
    bool ours = false;
    std::uint32_t version = 0;
    std::uint64_t size = 0;
};
[[nodiscard]] FrameHeader frameHeaderOf(std::span<const std::uint8_t> header, const Magic& magic,
                                        const std::string& what);

// The checks of openFrame on a frame of fileSize bytes (kFrameSize at least) with this header,
// checksumHolds telling whether its checksum is right (only asked when the sizes agree). Throws
// as openFrame.
void checkFrame(const FrameHeader& header, std::uint64_t fileSize, std::uint32_t current,
                const std::string& what, const std::function<bool()>& checksumHolds);

// Checks a frame, what naming the file in the messages (such as "world.meta"):
// - shorter than a frame, or shorter than the size it announces: Truncated;
// - longer than announced, or a wrong checksum: Corrupt;
// - another magic: Invalid ("not a world.meta file"), Corrupt if the checks above failed too;
// - version 0: Invalid; newer than current: NewerVersion.
[[nodiscard]] Frame openFrame(std::span<const std::uint8_t> bytes, const Magic& magic,
                              std::uint32_t current, const std::string& what);

} // namespace terrain::storage
