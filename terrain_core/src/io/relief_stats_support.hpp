#pragma once

// What the figures of the relief share, internal to terrain_core: cells of a class spread over
// the world, windows sampled around them, percentiles.

#include "terrain/io/relief_stats.hpp"

#include <cstddef>
#include <vector>

namespace terrain::relief_support {

// Up to count of the cells, spread evenly over them (every cell when there are fewer).
[[nodiscard]] std::vector<std::size_t> spread(const std::vector<std::size_t>& all, int count);

// Up to count cells holding bits, spread evenly over all of them.
[[nodiscard]] std::vector<std::size_t> spreadCells(const ReliefStats& stats, unsigned bits,
                                                   int count);

// The land cells in the mountain belts of the planetary geography (belt of at least 0.5); none
// without the geography.
[[nodiscard]] std::vector<std::size_t> beltCells(const Heightfield& field, const ReliefGrid& world,
                                                 const ReliefStats& stats);

// The value of rank floor(fraction * (n - 1)) in increasing order; 0 without any value.
[[nodiscard]] double percentile(std::vector<double> values, double fraction);

// Windows of side x side samples step meters apart centered on the cells, one per block on the
// threads.
[[nodiscard]] std::vector<ReliefGrid> windows(const Heightfield& field, const ReliefGrid& world,
                                              const std::vector<std::size_t>& cells, double step,
                                              int side, int threads);

} // namespace terrain::relief_support
