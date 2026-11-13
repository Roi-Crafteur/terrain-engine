#pragma once

#include "terrain/storage/edit_window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace terrain {

// The blocks of one edit in the cell of a point of a coarse lattice (CoarseEdits): their cover,
// and the box of those of the half-open cell (meters, the faces of their cubes).
struct CoarsePart {
    std::uint32_t weight = 0; // the cover, in eighths of a block (8 s^3 for a full cell)
    std::uint16_t count = 0;  // blocks of the half-open cell, in the box
    std::array<float, 3> low{};
    std::array<float, 3> high{};
};
using CoarseCell = std::array<CoarsePart, 2>; // 0: added, 1: removed

// The player edits as the coarse lattices see them: the tiles of LOD 1 to 3 (a sample every 2, 4
// or 8 m) and level 0 of the far horizon (agent 41). A lattice point no longer reads only the
// blocks that touch it, which showed a block of 1 m as a bump of a whole cell when it lay on a
// point and dropped it when it lay between two: each point P of spacing s answers for its cell.
//
// - The cover of a cell: the share of the blocks of each edit (added, removed) among the
//   (s + 1)^3 blocks within s / 2 of P, those of its sides counting half; an exact fraction.
// - What covers half a cell or more is drawn at its size: the density s (cover - 1/2), positive
//   inside the edit, exact on a plane wall (its zero at the wall), with the generated density by
//   min (added) or max (removed). It takes effect in proportion to the largest cover among the 27
//   cells around P, from 1/4 to 1/2: an edit smaller than a quarter of the cells around never
//   lifts nor sinks the ground there.
// - Below half a cell, a relay: the box of the blocks of each edit in the cells around P (blocks
//   b with P - s / 2 <= b < P + s / 2 on each axis), at its true size, with the generated density
//   by its true distance. A box thinner than the cell along x or z, with no block of its edit in
//   the cells beside it along that axis, is moved onto the column of its cell (at most s / 2,
//   never along y). On that column the ground side of the box is the edit's: the last sample
//   before its far face (the bottom of a hole open to the sky, the top of a block standing on the
//   ground) reads its distance to that face along y, so the face falls where it is whatever row
//   of the lattice the box lies on. A hole or a wall of one block keeps its depth or its height,
//   and its place within s / 2. Its weight: relay (1 at LOD 1, 1/2 at LOD 2, 0 at LOD 3), the
//   height of the box kept from the ground side: a wall of 3 m stands 1.5 m high at LOD 2, the
//   relay fading out.
// - Every added block first, then every removed one, as the LOD 0 does (ModificationStore).
//
// A pure function of the blocks within reach(s) of a point and of the generated density there:
// two tiles of a LOD, a tile and the horizon, a tile and the side of a finer tile stitched to it
// (stitchSeams) give a point the same bits. Without an edited block within reach it is empty and
// costs nothing.
class CoarseEdits {
public:
    // The samples (x0 + i s, y0 + j s, z0 + k s), i, j, k in [0, counts) on each axis, spacing
    // s >= 2 and even, from the blocks of window, which must hold every block within reach(s) of
    // them.
    CoarseEdits(const EditWindow& window, int x0, int y0, int z0, int spacing,
                std::array<int, 3> counts, float relay);

    [[nodiscard]] bool empty() const { return cells_.empty(); }
    [[nodiscard]] float density(float generated, int i, int j, int k) const;
    // The same, and whether a relay set it (rather than the cover of the cells or nothing).
    [[nodiscard]] float density(float generated, int i, int j, int k, bool& relayed) const;
    // The direction of the air of the relay of an edit (removed or added blocks) at the point at
    // (in samples from the first, any point near the sample (i, j, k)): into the nearest box of
    // a removed block, out of that of an added one, read from the cells around the sample. Zero
    // without a box there.
    [[nodiscard]] std::array<float, 3> relayDirection(bool removing, int i, int j, int k,
                                                      const std::array<double, 3>& at) const;

    // Meters from a point to the farthest block its density reads: the cells around it (1.5 s),
    // and with a relay the cells beside them (2.5 s: it moves a box only when they hold none of
    // its edit).
    [[nodiscard]] static constexpr int reach(int spacing, float relay) {
        return (relay > 0.0f ? 2 * spacing : spacing) + spacing / 2;
    }
    // The weight of the relay of a LOD: 1, 1/2, 0 at LOD 1, 2, 3 (0 at LOD 0, which reads blocks).
    [[nodiscard]] static constexpr float relayOfLod(int lod) {
        return lod == 1 ? 1.0f : (lod == 2 ? 0.5f : 0.0f);
    }

    [[nodiscard]] std::size_t heapBytes() const {
        return cells_.capacity() * sizeof(CoarseCell) + near_.capacity();
    }

private:
    class Sections; // the sections of the blocks around the samples (edit_coarse.cpp)

    [[nodiscard]] std::size_t index(int i, int j, int k) const;
    [[nodiscard]] const CoarseCell& cell(int i, int j, int k) const {
        return cells_[index(i, j, k)];
    }
    [[nodiscard]] float applyEdit(float density, std::size_t edit, int i, int j, int k,
                                  bool& relayed) const;
    [[nodiscard]] bool relays(const CoarsePart& part) const;
    [[nodiscard]] bool inRegion(int i, int j, int k) const;
    void markNear(int i, int j, int k);
    void snapBoxes();

    int x0_ = 0;
    int y0_ = 0;
    int z0_ = 0;
    int spacing_ = 2;
    std::array<int, 3> counts_{};
    float relay_ = 0.0f;
    int beyond_ = 2; // cells beyond each side of the samples: 2 with a relay, 1 without
    // The cells that may hold edited blocks (contentLow_ to contentHigh_, indices of the samples),
    // and those kept: these and two more around them, within the samples and beyond_ further.
    std::array<int, 3> contentLow_{};
    std::array<int, 3> contentHigh_{};
    std::array<int, 3> regionLow_{};
    std::array<int, 3> regionCount_{};
    std::vector<CoarseCell> cells_;  // the cells kept, x fastest
    std::vector<std::uint8_t> near_; // per cell: an edited block in it or in a cell around it
};

// The cells of a coarse lattice without relay, read from a window as asked and kept: the density
// of CoarseEdits with relay 0 at any point of the lattice, bit for bit, for the columns of the
// far horizon, which read the same cells from many columns (horizon_edit_columns.cpp). The window
// must hold the blocks within CoarseEdits::reach(spacing, 0) of the points asked.
class CoarseCellCache {
public:
    CoarseCellCache(const EditWindow& window, int spacing);
    ~CoarseCellCache();
    CoarseCellCache(const CoarseCellCache&) = delete;
    CoarseCellCache& operator=(const CoarseCellCache&) = delete;

    // The density at the lattice point (x, y, z) (multiples of the spacing).
    [[nodiscard]] float density(float generated, int x, int y, int z);

    // The covers of a height of a column: those of the cell of the point (x, y, z) and the largest
    // of the 9 cells of that height around it, of each edit (0: added, 1: removed).
    struct Level {
        std::array<float, 2> own{};
        std::array<float, 2> largest{};
    };
    [[nodiscard]] Level level(int x, int y, int z);
    // The density at a point from the levels under it, at it and over it: density, bit for bit.
    [[nodiscard]] float density(float generated, const Level& below, const Level& here,
                                const Level& above) const;

private:
    class Sections;
    struct KeyHash {
        std::size_t operator()(const std::array<int, 3>& key) const;
    };

    const CoarseCell& cellAt(const std::array<int, 3>& center);

    int spacing_;
    std::unique_ptr<Sections> sections_;
    std::unordered_map<std::array<int, 3>, CoarseCell, KeyHash> cells_;
};

} // namespace terrain
