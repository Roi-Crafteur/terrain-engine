#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain {

enum class EditShape : std::uint8_t {
    Box,       // the blocks anchor to anchor + size - 1
    Sphere,    // the blocks whose center lies within radius of the anchor
    Explosion, // a sphere whose edge is rough: within 90 % to 100 % of radius, block by block
};

enum class EditMode : std::uint8_t {
    Remove,  // the blocks become air (BlockEdit::Removed)
    Add,     // the blocks become rock (BlockEdit::Added)
    Restore, // the blocks are given back to the generated terrain
};

// Largest radius, in sixteenths of a meter: 4096 m.
inline constexpr std::uint16_t kMaxEditRadius = 65535;

// An edit of a volume of blocks, a few bytes long (serializeVolumeEdit), that every machine
// applies the same way: integers only. The blocks of its shape take its mode, the last edit of
// a block winning.
struct VolumeEdit {
    EditShape shape = EditShape::Sphere;
    EditMode mode = EditMode::Remove;
    BlockCoord anchor;                   // the lowest corner of a box, the center of a sphere
    std::array<std::uint16_t, 3> size{}; // box: blocks along x, y and z, at least 1 each
    std::uint16_t radius = 0;            // sphere, explosion: sixteenths of a meter, at least 1

    friend bool operator==(const VolumeEdit&, const VolumeEdit&) = default;
};

// Throws std::invalid_argument for an unknown shape or mode, an explosion that does not remove,
// a size or radius of 0, a shape beyond the 32-bit coordinates, or wider than the world (it would
// meet itself).
void validate(const VolumeEdit& edit, const WorldSize& world);

// True if the block lies in the shape of the edit, both in the same image of the world.
[[nodiscard]] bool contains(const VolumeEdit& edit, BlockCoord block);

// The blocks the shape may hold, bounds included, in the image of its anchor.
struct BlockBox {
    BlockCoord min;
    BlockCoord max;

    friend bool operator==(const BlockBox&, const BlockBox&) = default;
};
[[nodiscard]] BlockBox editBounds(const VolumeEdit& edit);

// Bytes of an edit, little-endian: u8 shape * 16 + mode, i32 x, i32 y, i32 z of the anchor,
// then u16 x, y and z of a box (19 bytes) or u16 radius of a sphere or an explosion (15 bytes).
inline constexpr std::size_t kBoxEditBytes = 19;
inline constexpr std::size_t kRoundEditBytes = 15;
[[nodiscard]] std::vector<std::uint8_t> serializeVolumeEdit(const VolumeEdit& edit);
// Throws a StorageError (Invalid) unless bytes are a whole edit of a known shape and mode, with
// sizes and a radius of at least 1 and an explosion that removes.
[[nodiscard]] VolumeEdit deserializeVolumeEdit(std::span<const std::uint8_t> bytes);

// Applies an edit given at any image of the world: its blocks are stored at their canonical
// image, by sections (a section the shape covers whole becomes filled or empty at once).
// Returns the canonical chunks whose tiles change (blockChunks of every block of its bounds),
// sorted by z, then x. Throws as validate, before any change.
std::vector<ChunkCoord> applyVolumeEdit(ModificationStore& store, const VolumeEdit& edit,
                                        const WorldSize& world);

// An edit and its place in the order every machine applies them in.
struct SequencedEdit {
    std::uint64_t sequence = 0;
    VolumeEdit edit;
};

// Applies the edits by increasing sequence, whatever their order in edits: the same edits give
// the same store, and the same bytes. Returns the chunks of every edit, sorted, once each.
// Throws std::invalid_argument, before any change, for a repeated sequence or an invalid edit.
std::vector<ChunkCoord> applyInSequence(ModificationStore& store,
                                        std::span<const SequencedEdit> edits,
                                        const WorldSize& world);

} // namespace terrain
