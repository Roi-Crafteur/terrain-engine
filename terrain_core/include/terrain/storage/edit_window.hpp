#pragma once

#include "terrain/generation/vegetation.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace terrain {

// The edited blocks centered in an area of a store, at every height, copied from it so that they
// are read without its lock (the sections of the chunks of the area). The area may lie at any
// image of the wrapped world, narrower than it: the window reads the canonical chunks and
// answers in the image of the area. Reads give what the store gave for the blocks of the area,
// nothing outside it.
class EditWindow {
public:
    EditWindow() = default;
    // The blocks centered in [minX, maxX] x [minZ, maxZ], meters, bounds included.
    EditWindow(const ModificationStore& store, int minX, int maxX, int minZ, int maxZ,
               const WorldSize& world);

    [[nodiscard]] bool empty() const { return !edited_; }
    [[nodiscard]] std::optional<BlockEdit> blockAt(int x, int y, int z) const;

    // Density at the lattice point (x, y, z) after the edits of the blocks within reach:
    // ModificationStore::editedDensity, bit for bit.
    [[nodiscard]] float density(float generated, int x, int y, int z) const;

    // The lowest and highest y of the edited blocks of each section of the area, by section.
    [[nodiscard]] std::vector<std::pair<int, int>> editedSpans() const;

    // True if every block of [min, max] lies in a filled section, all filled with one edit: then
    // no lattice point among them but on the edge of the box changes sign whatever the terrain.
    [[nodiscard]] bool filled(BlockCoord min, BlockCoord max) const;

    // The section sy of the chunk column (cx, cz) of the image of the area, or nullptr.
    [[nodiscard]] const BlockSection* section(std::int32_t cx, std::int32_t sy,
                                              std::int32_t cz) const;
    // True if a section of the area meets the blocks [min, max]: false means no edited block
    // among them.
    [[nodiscard]] bool meets(BlockCoord min, BlockCoord max) const;
    // True if a section of the area meeting the blocks [min, max] holds a block of this edit:
    // false means none among them.
    [[nodiscard]] bool holds(BlockEdit edit, BlockCoord min, BlockCoord max) const;

    // Bytes the window holds on the heap.
    [[nodiscard]] std::size_t heapBytes() const;

private:
    friend class EditLattice;

    struct Column {
        std::vector<std::int32_t> ys; // increasing
        std::vector<BlockSection> sections;
    };

    [[nodiscard]] const Column* column(std::int32_t cx, std::int32_t cz) const;
    [[nodiscard]] bool inArea(int x, int z) const {
        return x >= minX_ && x <= maxX_ && z >= minZ_ && z <= maxZ_;
    }

    int minX_ = 0;
    int maxX_ = -1;
    int minZ_ = 0;
    int maxZ_ = -1;
    std::int32_t cx0_ = 0;
    std::int32_t cz0_ = 0;
    int nx_ = 0;
    int nz_ = 0;
    std::vector<Column> columns_; // x fastest
    bool edited_ = false;
};

// The states of the blocks within reach of a cube of lattice samples (x0 + i s, y0 + j s,
// z0 + k s), i, j and k in [0, count), count at most 21, read once from a window: the edited
// densities of the samples, as EditWindow::density gives them one by one. The states are held
// as bits, one row of blocks along x per word; each sample is classified once: no edited block
// around it, all of them removed, all added, or a mixture read block by block.
class EditLattice {
public:
    EditLattice(const EditWindow& window, int x0, int y0, int z0, int spacing, int count);

    // No edited block reaches the samples.
    [[nodiscard]] bool empty() const { return kinds_.empty(); }
    [[nodiscard]] float density(float generated, int i, int j, int k) const;
    [[nodiscard]] std::size_t heapBytes() const {
        return (removed_.capacity() + added_.capacity()) * sizeof(std::uint64_t) +
               kinds_.capacity();
    }

private:
    // Along an axis: the block coordinates within reach of the samples, sorted; sample i reaches
    // the 3 coordinates from first(i) on.
    struct Axis {
        int origin = 0;
        int spacing = 1;
        int count = 0;
        [[nodiscard]] bool spaced() const { return spacing > 2; }
        [[nodiscard]] int size() const { return spaced() ? 3 * count : (count - 1) * spacing + 3; }
        [[nodiscard]] int first(int i) const { return spaced() ? 3 * i : i * spacing; }
        [[nodiscard]] int coordinate(int index) const;
        // Index of a block coordinate, -1 if no sample reaches it.
        [[nodiscard]] int indexOf(int coordinate) const;
        // Indices of the coordinates in [low, high]: first > last if none.
        [[nodiscard]] std::pair<int, int> range(int low, int high) const;
    };

    void fill(const EditWindow& window, std::int32_t cx, std::int32_t sy, std::int32_t cz);
    void fillRows(const SectionMask& removed, const SectionMask& added, BlockCoord base,
                  std::pair<int, int> xs, std::pair<int, int> ys, std::pair<int, int> zs);
    // The kind of each sample, from the rows.
    void classify();
    [[nodiscard]] std::size_t row(int iy, int iz) const {
        return static_cast<std::size_t>(iz * y_.size() + iy);
    }

    Axis x_;
    Axis y_;
    Axis z_;
    std::vector<std::uint64_t> removed_; // per row of blocks along x: bit ix if removed
    std::vector<std::uint64_t> added_;
    std::vector<std::uint8_t> kinds_; // per sample, i fastest, then j
};

// The block under the foot of a plant: the lattice point just below its foot lies at its center.
[[nodiscard]] BlockCoord plantFootBlock(const Plant& plant);

// Drops the generated plants whose foot block is removed (their ground is gone), in the image
// of the window. The rule reads only the edits and the position of each plant, never the LOD of
// a tile: every placement of the generated plants goes through it, near and far alike.
void dropPlantsOnRemovedGround(const EditWindow& window, std::vector<Plant>& plants);

} // namespace terrain
