#pragma once

// The bands of the rock of the samples of the far horizon (agent 33): for the GPU horizon to draw
// the beds, the notches and the caps of the faces of rock its heights only ramp (horizon_sampler
// .hpp), at the heights the materials of the tiles draw them (TileData::rockBands): the band
// channel of the rock, generic (the faces of the coasts and of the walls of the mountains, the
// spires of the stacks).

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <span>
#include <vector>

namespace terrain {

// The bands of the samples of a rectangle of a level, row by row as cells (sampled on field: a
// HorizonSampler's, of the same rectangle): rockBands of the rock of each sample at its height
// (the columns of Heightfield::rockAt, read in one batch by samplesAt: the same bits) where its
// surface is bare rock (its material: rock), above the water or under it (the foot of a wall in
// the sea keeps its notch: agent 40), none elsewhere. Thread-safe. Throws
// std::invalid_argument for cells of another count.
[[nodiscard]] std::vector<RockBands> horizonBands(const Heightfield& field,
                                                  const HorizonParams& params, int level,
                                                  const HorizonRect& rect,
                                                  std::span<const HorizonCell> cells);

} // namespace terrain
