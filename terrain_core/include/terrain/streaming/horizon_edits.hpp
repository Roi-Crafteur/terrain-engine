#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/streaming/horizon_edit_columns.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace terrain {

class TerrainTiles;

// The player edits as every level of the far horizon sees them: the edited ground of the columns
// of the LOD 3 lattice (horizon_edit_columns.hpp), summed in a sparse pyramid of aligned squares
// of 2^m x 2^m columns. The footprint of a sample of pitch p, [c - p / 2, c + p / 2) on each
// axis, holds p / 8 columns per side: the column of the sample itself at level 0 (the ground of
// the tiles there), else 2 x 2 squares of the pyramid. Its summary (HorizonSampler) is the mean
// change of the ground over the footprint (edited ground minus generated ground, 0 where a column
// keeps its ground) and, when every column of it is edited, the range of the edited ground, which
// bounds the sample: a razed floor is flat on every level, the crests of the generated relief
// gone, and level 0 is the ground of the tiles bit for bit.
//
// Every value is a pure function of the edits and the seed: a square sums its 4 children in a
// fixed order whenever one of them changes, so the order of the edits never changes a bit.
//
// The edits take effect in two steps, both on the thread owning the map: stage reads the edited
// ground of the columns of chunks at once (from the edits as they are then), applyStaged makes
// what was staged visible to the samplers, which may only be done while no sample is computed:
// the drivers of the horizon (HorizonStreamer, the Unreal bridge) call it between two epochs,
// then invalidate the areas it returns. Reading (footprint, summary, the source) is thread-safe
// between two applyStaged.
struct FootprintEdits {
    std::int64_t columns = 0; // of the footprint
    std::int64_t edited = 0;  // of them, whose ground the edits changed
    double change = 0.0;      // sum of their changes (edited minus generated ground), meters
    float low = 0.0f;         // lowest and highest edited ground, with an edited column
    float high = 0.0f;
};

// An area of the world, meters, whose samples must be computed again (HorizonClipmap::invalidate).
struct HorizonArea {
    double minX = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;

    friend bool operator==(const HorizonArea&, const HorizonArea&) = default;
};

class HorizonEditMap {
public:
    // The squares of the pyramid go up to the footprints of the coarsest pitch validate allows.
    static constexpr int kSquareLevels = 12;

    // field: the relief of the tiles (their Heightfield), which must outlive the map.
    explicit HorizonEditMap(const Heightfield& field);

    // Reads the edited ground of the columns of chunks (editedColumns) and keeps it for the next
    // applyStaged; a column staged twice keeps its last ground. From the tiles (the chunks
    // their edits return), a store or any window.
    void stage(const TerrainTiles& tiles, std::span<const ChunkCoord> chunks);
    void stage(const ModificationStore& store, std::span<const ChunkCoord> chunks);
    void stage(std::span<const ChunkCoord> chunks, const EditWindowSource& windows);
    [[nodiscard]] bool hasStaged() const { return !staged_.empty(); }

    // The staged columns take effect: returns the areas around the columns whose ground changed
    // (canonical), one per group of nearby columns. Never while a sample is computed.
    std::vector<HorizonArea> applyStaged();

    // The edits within the footprint of a sample (HorizonSampler: canonical center, half a pitch
    // of a multiple of 8 m), and its summary: nullopt without an edited column. Throws
    // std::invalid_argument for another footprint.
    [[nodiscard]] FootprintEdits footprint(const CellFootprint& sample) const;
    [[nodiscard]] std::optional<EditSummary> summary(const CellFootprint& sample) const;
    // summary as an EditSummarySource; the map must outlive it.
    [[nodiscard]] EditSummarySource source() const;

    // The edited ground of a canonical column, nullopt where it keeps its ground.
    [[nodiscard]] std::optional<float> columnGround(EditColumn column) const;
    [[nodiscard]] std::size_t columnCount() const { return leaves_.size(); }
    [[nodiscard]] bool empty() const { return leaves_.empty(); }
    // Bytes the map holds on the heap: its columns and squares (the nodes of their tables counted
    // as their value and 2 words, the buckets as 1 word), what is staged aside.
    [[nodiscard]] std::size_t heapBytes() const;

    // Throws std::invalid_argument unless samplers of these parameters can read the map: the
    // world of its relief, and a pitch of level 0 that is a multiple of 8 m.
    void validate(const HorizonParams& params) const;

    [[nodiscard]] const WorldSize& world() const { return field_->world(); }

private:
    struct Column {
        float edited = 0.0f;
        float ground = 0.0f;

        friend bool operator==(const Column&, const Column&) = default;
    };
    struct Square {
        std::uint32_t edited = 0;
        double change = 0.0;
        float low = 0.0f;
        float high = 0.0f;
    };
    using Squares = std::unordered_map<std::uint64_t, Square>;

    void updateSquares(std::vector<EditColumn> changed);
    [[nodiscard]] Square squareAt(int level, std::int64_t x, std::int64_t z) const;
    [[nodiscard]] std::vector<HorizonArea> areasOf(const std::vector<EditColumn>& changed) const;

    const Heightfield* field_;
    std::int64_t countX_;
    std::int64_t countZ_;
    std::unordered_map<std::uint64_t, Column> leaves_; // the edited columns
    std::vector<Squares> squares_; // levels 1 to kSquareLevels - 1 (index level - 1)
    std::map<EditColumn, std::optional<Column>> staged_;
};

} // namespace terrain
