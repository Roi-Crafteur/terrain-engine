#include "terrain/storage/volume_edit.hpp"

#include "byte_stream.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>

namespace terrain {

// Sections and chunks share their columns of 16 x 16 blocks.
static_assert(kChunkSizeMeters == kSectionSide);

namespace {

// The edge of an explosion lies between kRoughness / 256 and 1 of its radius.
constexpr std::int64_t kRoughness = 230;
constexpr std::int64_t kFull = 256;

std::uint64_t mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::uint64_t bits32(std::int64_t v) {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(v));
}

// The edge of an explosion at the offset (dx, dy, dz) of its anchor, out of 256: the same for
// every image of the world, whose sizes are multiples of 65,536 m.
std::int64_t roughEdge(const VolumeEdit& edit, std::int64_t dx, std::int64_t dy, std::int64_t dz) {
    const std::uint64_t anchor = (bits32(edit.anchor.x) & 0xFFFFU) |
                                 ((bits32(edit.anchor.z) & 0xFFFFU) << 16) |
                                 (bits32(edit.anchor.y) << 32);
    std::uint64_t key = mix(anchor ^ (static_cast<std::uint64_t>(edit.radius) << 48));
    key = mix(key ^ (bits32(dx) | (bits32(dy) << 32)));
    key = mix(key ^ bits32(dz));
    return kRoughness + static_cast<std::int64_t>(key % (kFull - kRoughness + 1));
}

// Blocks from the anchor to the edge of a sphere of radius sixteenths of a meter.
std::int64_t reachOf(std::uint16_t radius) {
    return radius / 16;
}

// Squared distance, sixteenths of a meter squared, for distance squared d2 in meters.
std::int64_t scaled(std::int64_t d2) {
    return 256 * d2;
}

enum class Cover { None, Part, All };

std::int64_t squared(std::int64_t v) {
    return v * v;
}

// How much of the box b (in the image of the anchor) the shape holds.
Cover coverOf(const VolumeEdit& edit, const BlockBox& b) {
    const BlockBox bounds = editBounds(edit);
    const std::array<std::int64_t, 3> low = {b.min.x, b.min.y, b.min.z};
    const std::array<std::int64_t, 3> high = {b.max.x, b.max.y, b.max.z};
    const std::array<std::int64_t, 3> min = {bounds.min.x, bounds.min.y, bounds.min.z};
    const std::array<std::int64_t, 3> max = {bounds.max.x, bounds.max.y, bounds.max.z};
    if (edit.shape == EditShape::Box) {
        bool inside = true;
        for (std::size_t a = 0; a < 3; ++a) {
            if (high[a] < min[a] || low[a] > max[a]) {
                return Cover::None;
            }
            inside = inside && low[a] >= min[a] && high[a] <= max[a];
        }
        return inside ? Cover::All : Cover::Part;
    }
    const std::array<std::int64_t, 3> center = {edit.anchor.x, edit.anchor.y, edit.anchor.z};
    std::int64_t nearest = 0;
    std::int64_t farthest = 0;
    for (std::size_t a = 0; a < 3; ++a) {
        nearest += squared(std::clamp(center[a], low[a], high[a]) - center[a]);
        farthest += squared(std::max(std::abs(low[a] - center[a]), std::abs(high[a] - center[a])));
    }
    const std::int64_t r2 = squared(edit.radius);
    if (scaled(nearest) > r2) {
        return Cover::None;
    }
    const bool rough = edit.shape == EditShape::Explosion;
    const bool all = rough ? scaled(farthest) * kFull * kFull <= r2 * kRoughness * kRoughness
                           : scaled(farthest) <= r2;
    return all ? Cover::All : Cover::Part;
}

std::optional<BlockEdit> stateOf(EditMode mode) {
    switch (mode) {
    case EditMode::Remove:
        return BlockEdit::Removed;
    case EditMode::Add:
        return BlockEdit::Added;
    case EditMode::Restore:
        break;
    }
    return std::nullopt;
}

// Applies the edit to the section sy of the chunk (cx, cz) of the image of its anchor, stored in
// the canonical chunk.
void applyToSection(ModificationStore& store, const VolumeEdit& edit, ChunkCoord frame,
                    ChunkCoord canonical, std::int32_t sy) {
    const BlockCoord low{frame.x * kSectionSide, sy * kSectionSide, frame.z * kSectionSide};
    const BlockBox box{low, {low.x + kSectionSide - 1, low.y + kSectionSide - 1,
                             low.z + kSectionSide - 1}};
    const Cover cover = coverOf(edit, box);
    const std::optional<BlockEdit> state = stateOf(edit.mode);
    if (cover == Cover::None) {
        return;
    }
    if (cover == Cover::All) {
        store.setSection(canonical, sy, state ? BlockSection::filled(*state) : BlockSection());
        return;
    }
    SectionMask removed{};
    SectionMask added{};
    if (const BlockSection* current = store.section(canonical, sy)) {
        current->toMasks(removed, added);
    }
    for (int y = 0; y < kSectionSide; ++y) {
        for (int z = 0; z < kSectionSide; ++z) {
            for (int x = 0; x < kSectionSide; ++x) {
                if (!contains(edit, {low.x + x, low.y + y, low.z + z})) {
                    continue;
                }
                const int index = sectionIndex(x, y, z);
                const auto word = static_cast<std::size_t>(index / 64);
                const std::uint64_t bit = std::uint64_t{1} << (index % 64);
                removed[word] &= ~bit;
                added[word] &= ~bit;
                if (state) {
                    (*state == BlockEdit::Removed ? removed : added)[word] |= bit;
                }
            }
        }
    }
    store.setSection(canonical, sy, BlockSection::fromMasks(removed, added));
}

std::int32_t canonicalMeter(std::int32_t meters, double period) {
    return static_cast<std::int32_t>(wrapIndex(meters, static_cast<std::int64_t>(period)));
}

void sortUnique(std::vector<ChunkCoord>& chunks) {
    std::sort(chunks.begin(), chunks.end(), ChunkOrder{});
    chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());
}

} // namespace

void validate(const VolumeEdit& edit, const WorldSize& world) {
    if (edit.shape > EditShape::Explosion || edit.mode > EditMode::Restore) {
        throw std::invalid_argument("unknown shape or mode of edit");
    }
    if (edit.shape == EditShape::Explosion && edit.mode != EditMode::Remove) {
        throw std::invalid_argument("an explosion only removes blocks");
    }
    const bool box = edit.shape == EditShape::Box;
    if (box ? (edit.size[0] == 0 || edit.size[1] == 0 || edit.size[2] == 0) : edit.radius == 0) {
        throw std::invalid_argument("edit of size 0");
    }
    constexpr std::int64_t kLowest = std::numeric_limits<std::int32_t>::min();
    constexpr std::int64_t kHighest = std::numeric_limits<std::int32_t>::max();
    const std::int64_t reach = reachOf(edit.radius);
    const std::array<std::int64_t, 3> anchor = {edit.anchor.x, edit.anchor.y, edit.anchor.z};
    for (std::size_t a = 0; a < 3; ++a) {
        const std::int64_t low = box ? anchor[a] : anchor[a] - reach;
        const std::int64_t high = box ? anchor[a] + edit.size[a] - 1 : anchor[a] + reach;
        if (low < kLowest || high > kHighest) {
            throw std::invalid_argument("edit beyond the 32-bit coordinates");
        }
    }
    const auto width = static_cast<double>(box ? edit.size[0] : 2 * reach + 1);
    const auto depth = static_cast<double>(box ? edit.size[2] : 2 * reach + 1);
    if (width >= world.x || depth >= world.z) {
        throw std::invalid_argument("edit wider than the world");
    }
}

bool contains(const VolumeEdit& edit, BlockCoord block) {
    const std::int64_t dx = std::int64_t{block.x} - edit.anchor.x;
    const std::int64_t dy = std::int64_t{block.y} - edit.anchor.y;
    const std::int64_t dz = std::int64_t{block.z} - edit.anchor.z;
    if (edit.shape == EditShape::Box) {
        return dx >= 0 && dy >= 0 && dz >= 0 && dx < edit.size[0] && dy < edit.size[1] &&
               dz < edit.size[2];
    }
    const std::int64_t d2 = scaled(dx * dx + dy * dy + dz * dz);
    const std::int64_t r2 = squared(edit.radius);
    if (edit.shape == EditShape::Sphere || d2 > r2) {
        return d2 <= r2;
    }
    const std::int64_t edge = roughEdge(edit, dx, dy, dz);
    return d2 * kFull * kFull <= r2 * edge * edge;
}

BlockBox editBounds(const VolumeEdit& edit) {
    // Within 32 bits for a valid edit (validate).
    const auto at = [](std::int64_t v) { return static_cast<std::int32_t>(v); };
    if (edit.shape == EditShape::Box) {
        return {edit.anchor,
                {at(std::int64_t{edit.anchor.x} + edit.size[0] - 1),
                 at(std::int64_t{edit.anchor.y} + edit.size[1] - 1),
                 at(std::int64_t{edit.anchor.z} + edit.size[2] - 1)}};
    }
    const std::int64_t reach = reachOf(edit.radius);
    return {{at(edit.anchor.x - reach), at(edit.anchor.y - reach), at(edit.anchor.z - reach)},
            {at(edit.anchor.x + reach), at(edit.anchor.y + reach), at(edit.anchor.z + reach)}};
}

std::vector<std::uint8_t> serializeVolumeEdit(const VolumeEdit& edit) {
    const bool box = edit.shape == EditShape::Box;
    storage::ByteWriter out(box ? kBoxEditBytes : kRoundEditBytes);
    out.u8(static_cast<std::uint8_t>(static_cast<unsigned>(edit.shape) * 16U +
                                     static_cast<unsigned>(edit.mode)));
    out.i32(edit.anchor.x);
    out.i32(edit.anchor.y);
    out.i32(edit.anchor.z);
    if (box) {
        for (const std::uint16_t side : edit.size) {
            out.u16(side);
        }
    } else {
        out.u16(edit.radius);
    }
    return out.take();
}

VolumeEdit deserializeVolumeEdit(std::span<const std::uint8_t> bytes) {
    storage::ByteReader in(bytes, "invalid volume edit: ");
    const std::uint8_t head = in.u8();
    VolumeEdit edit;
    edit.shape = static_cast<EditShape>(head / 16U);
    edit.mode = static_cast<EditMode>(head % 16U);
    if (edit.shape > EditShape::Explosion || edit.mode > EditMode::Restore) {
        in.fail("unknown shape or mode");
    }
    edit.anchor.x = in.i32();
    edit.anchor.y = in.i32();
    edit.anchor.z = in.i32();
    if (edit.shape == EditShape::Box) {
        for (std::uint16_t& side : edit.size) {
            side = in.u16();
        }
    } else {
        edit.radius = in.u16();
    }
    in.expectEnd("trailing bytes");
    try {
        validate(edit, WorldSize{});
    } catch (const std::invalid_argument& error) {
        in.fail(error.what());
    }
    return edit;
}

std::vector<ChunkCoord> applyVolumeEdit(ModificationStore& store, const VolumeEdit& edit,
                                        const WorldSize& world) {
    VolumeEdit local = edit;
    local.anchor.x = canonicalMeter(edit.anchor.x, world.x);
    local.anchor.z = canonicalMeter(edit.anchor.z, world.z);
    validate(local, world);
    const BlockBox bounds = editBounds(local);
    for (std::int32_t cz = bounds.min.z >> kSectionShift; cz <= bounds.max.z >> kSectionShift;
         ++cz) {
        for (std::int32_t cx = bounds.min.x >> kSectionShift;
             cx <= bounds.max.x >> kSectionShift; ++cx) {
            const ChunkCoord canonical = wrapChunk({cx, cz}, world);
            for (std::int32_t sy = sectionOf(bounds.min.y); sy <= sectionOf(bounds.max.y);
                 ++sy) {
                applyToSection(store, local, {cx, cz}, canonical, sy);
            }
        }
    }
    // As blockChunks: the chunks of the lattice points within reach of the bounds.
    std::vector<ChunkCoord> chunks;
    const ChunkCoord first = worldToChunk(bounds.min.x - kBlockReach - 1,
                                          bounds.min.z - kBlockReach - 1);
    const ChunkCoord last = worldToChunk(bounds.max.x + kBlockReach, bounds.max.z + kBlockReach);
    for (std::int32_t z = first.z; z <= last.z; ++z) {
        for (std::int32_t x = first.x; x <= last.x; ++x) {
            chunks.push_back(wrapChunk({x, z}, world));
        }
    }
    sortUnique(chunks);
    return chunks;
}

std::vector<ChunkCoord> applyInSequence(ModificationStore& store,
                                        std::span<const SequencedEdit> edits,
                                        const WorldSize& world) {
    std::vector<SequencedEdit> ordered(edits.begin(), edits.end());
    std::sort(ordered.begin(), ordered.end(), [](const SequencedEdit& a, const SequencedEdit& b) {
        return a.sequence < b.sequence;
    });
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (i > 0 && ordered[i].sequence == ordered[i - 1].sequence) {
            throw std::invalid_argument("two edits share a sequence");
        }
        VolumeEdit canonical = ordered[i].edit;
        canonical.anchor.x = canonicalMeter(canonical.anchor.x, world.x);
        canonical.anchor.z = canonicalMeter(canonical.anchor.z, world.z);
        validate(canonical, world);
    }
    std::vector<ChunkCoord> chunks;
    for (const SequencedEdit& edit : ordered) {
        const std::vector<ChunkCoord> part = applyVolumeEdit(store, edit.edit, world);
        chunks.insert(chunks.end(), part.begin(), part.end());
    }
    sortUnique(chunks);
    return chunks;
}

} // namespace terrain
