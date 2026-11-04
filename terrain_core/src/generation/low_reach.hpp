#pragma once

// A lattice of the world telling which sites may reach a point (agent 52), internal to
// terrain_core: square cells of a fixed side, each holding the indices of the sites whose reach
// (a disc) touches it, in increasing order, as many as there are (no limit per cell). Built once,
// in the order of the sites: the same lattice whatever the threads.

#include "terrain/world_size.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace terrain::lows {

class ReachLattice {
public:
    ReachLattice() = default;
    // Cells of about cell meters: a whole number of them along each side of the world.
    ReachLattice(const WorldSize& world, double cell);

    // Marks the cells the disc of radius reach around the canonical (x, z) touches, for site
    // index (indices added in increasing order), then builds the lattice after the last one.
    void add(std::uint32_t index, double x, double z, double reach);
    void finish();

    // The sites whose reach may hold the canonical (x, z), in increasing order.
    [[nodiscard]] std::span<const std::uint32_t> at(double x, double z) const;
    [[nodiscard]] bool empty() const { return indices_.empty(); }

private:
    [[nodiscard]] int cellX(double x) const;
    [[nodiscard]] int cellZ(double z) const;

    WorldSize world_;
    double sizeX_ = 1.0;
    double sizeZ_ = 1.0;
    int countX_ = 1;
    int countZ_ = 1;
    std::vector<std::uint64_t> marks_; // (cell << 32) | index, until finish
    std::vector<std::uint32_t> offsets_; // countX * countZ + 1
    std::vector<std::uint32_t> indices_;
};

} // namespace terrain::lows
