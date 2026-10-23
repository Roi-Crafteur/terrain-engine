#pragma once

// Record of a chunk in the region files of format 2, by sections. Shared by the files of
// src/storage only.
//
// Record, little-endian:
//   i32 x, i32 z, u32 size of the rest of the record, then:
//   varint entry count, then the entries of the sections by increasing y:
//     varint: the y of the first section of the first entry (zigzag), then for the next ones
//       the sections skipped since the end of the previous entry;
//     u8 kind:
//       1 (removed) or 2 (added): a run of filled sections, varint count (at least 1), never
//         followed at once by a run of the same kind;
//       3 (list): up to kListLimit edited blocks, varint count (at least 1), then per block by
//         increasing index: varint (index - previous index - 1) * 2 + 1 if removed (the
//         previous index of the first block is -1);
//       4 (coded): more than kListLimit edited blocks, not all the same, coded in the masks;
//   varint size of the masks, the masks (section_codec.hpp) of the coded sections by
//   increasing y, empty without any;
//   the plants of the chunk (storage_codec.hpp: writePlants).
// The same modifications always give the same bytes.

#include "byte_stream.hpp"
#include "terrain/chunk_coord.hpp"
#include "terrain/storage/modifications.hpp"

#include <cstddef>

namespace terrain::storage {

inline constexpr int kListLimit = 16;
// A record without any modification: its coordinates, its size, two empty counts and two
// empty plant counts.
inline constexpr std::size_t kMinChunkRecordV2 = 22;

void writeChunkV2(ByteWriter& out, ChunkCoord coord, const ChunkModifications& chunk);

// Reads the record of the chunk coord (its coordinates already read: readChunkCoord), which
// store does not hold yet. Rejects a size beyond the bytes, an unknown kind, sections out of
// order or beyond the 32-bit heights, runs that should be one, a list or coded section of the
// wrong size or filled, masks not read exactly, the records of the plants (readPlants), bytes
// left in the record and a chunk without modifications.
void readChunkBodyV2(ByteReader& in, ChunkCoord coord, ModificationStore& store);

} // namespace terrain::storage
