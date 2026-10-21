#pragma once

#include "terrain/streaming/horizon_grids.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace terrain {

// Samples computed together by default: a few milliseconds of work.
inline constexpr std::size_t kHorizonPieceSamples = 4096;

// Samples of a level to compute, with their cells once computed (rect.count(), row by row).
struct HorizonPiece {
    int level = 0;
    HorizonRect rect;
    std::vector<HorizonCell> cells;
};

// One update of every level of a clipmap: the window each level moves to and the pieces to
// compute, the samples entering the windows and the samples of the invalidated areas. A
// renderer computes the pieces on its own threads (computePiece), in any order and on any
// thread, then commits the epoch on the thread owning the clipmap: every level then moves at
// once, so the levels shown are always complete and nested.
struct HorizonEpoch {
    std::uint64_t base = 0;            // commits of the clipmap it was planned after
    std::vector<HorizonIndex> centers; // the new center of every level (continuous)
    std::vector<HorizonPiece> pieces;  // finest levels first
    std::uint64_t invalidations = 0;   // the invalidations it covers: those up to this one

    [[nodiscard]] std::int64_t samples() const; // to compute
};

// Computes the cells of a piece. Thread-safe: pieces of an epoch may be computed concurrently.
void computePiece(const HorizonSampler& sampler, HorizonPiece& piece);

// What a commit changed in a level: whether its window moved, and the samples written
// (continuous indices, as the window; slotRects gives their slots).
struct HorizonLevelChange {
    int level = 0;
    bool moved = false;
    std::vector<HorizonRect> written;
};

struct HorizonChanges {
    std::vector<HorizonLevelChange> levels; // the levels that changed, finest first

    [[nodiscard]] bool empty() const { return levels.empty(); }
};

// The levels of a far horizon around an observer, and the areas whose samples must be computed
// again. Every method must be called from the thread owning the clipmap; only computePiece runs
// elsewhere. Once every planned epoch is committed, the samples only depend on the last
// position given to plan, the sampler and the invalidations: never on the path of the observer,
// the order of the pieces or the threads.
class HorizonClipmap {
public:
    // Throws std::invalid_argument for invalid parameters (validate).
    explicit HorizonClipmap(const HorizonParams& params);

    // The epoch bringing every level to the windows around the observer at (x, z), any image,
    // and computing the invalidated samples again, or nullopt when nothing would change. The
    // rectangles are cut into pieces of whole rows of about pieceSamples samples. Throws
    // std::invalid_argument for a non-finite position or pieceSamples 0.
    [[nodiscard]] std::optional<HorizonEpoch>
    plan(double x, double z, std::size_t pieceSamples = kHorizonPieceSamples) const;

    // Applies an epoch planned after the last commit, its pieces computed: moves the windows,
    // writes the pieces and forgets the invalidations it covers. Throws std::invalid_argument
    // for another epoch (a stale one, or one with a piece not computed).
    HorizonChanges commit(const HorizonEpoch& epoch);

    // The samples whose footprint, or a neighbor's (their gradients), meets [minX, maxX] x
    // [minZ, maxZ] (any image, narrower than the world) are computed again by the next epochs,
    // on every level: call it once the edit summaries of the area changed. Throws
    // std::invalid_argument for a non-finite or reversed area, or one as wide as the world.
    void invalidate(double minX, double minZ, double maxX, double maxZ);

    [[nodiscard]] const HorizonParams& params() const { return params_; }
    [[nodiscard]] int levelCount() const { return static_cast<int>(levels_.size()); }
    [[nodiscard]] const HorizonLevel& level(int level) const;
    [[nodiscard]] bool placed() const { return commits_ > 0; }
    [[nodiscard]] std::uint64_t commits() const { return commits_; }
    // Invalidated areas not covered by a committed epoch yet.
    [[nodiscard]] std::size_t pendingInvalidations() const { return dirty_.size(); }
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    struct Area {
        std::uint64_t id = 0;
        double minX = 0.0;
        double minZ = 0.0;
        double maxX = 0.0;
        double maxZ = 0.0;
    };

    void planLevel(int level, double x, double z, std::size_t pieceSamples,
                   HorizonEpoch& epoch) const;
    [[nodiscard]] HorizonRect areaSamples(const Area& area, int level, HorizonIndex center) const;

    HorizonParams params_;
    std::vector<HorizonLevel> levels_;
    std::vector<Area> dirty_;
    std::uint64_t nextArea_ = 1;
    std::uint64_t commits_ = 0;
};

} // namespace terrain
