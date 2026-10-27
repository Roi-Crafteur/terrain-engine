#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_fine_grid.hpp"
#include "terrain/streaming/horizon_levels.hpp"

#include <span>
#include <vector>

namespace terrain {

// Crest prefilter of the far horizon. A sample of level n stands for the ground of the cells
// around it, pitch meters wide; its point height misses the crests and summits between the
// samples, so far mountains show soft silhouettes, a little too low. From level 1 on, with
// HorizonParams::crestRatio R > 0, each sample rises by its lift, the largest its 4 cells give:
//
// - the top M of a cell (the square between 4 neighbor samples) is the highest height of the
//   Heightfield (heightAt, no player edit) along its 4 sides every pitch / R meters, its corners
//   included, and at its center: 4 R + 1 points, as a crest crosses the sides of the cells it
//   runs through;
// - the highest corner of the cell (each of them on a tie) rises to M, by M minus its point
//   height; the other corners keep theirs.
//
// On a plane, the top of a cell is its highest corner: nothing rises (a max filter would lift
// every slope by half a pitch and fill the valleys narrower than a pitch). A sample lower than
// another corner of each of its 4 cells never rises: a valley floor, lower than both of its
// neighbors along an axis, keeps its point height, whatever crest stands over it. Along a ridge,
// the higher corner of each cell reaches the crest. A sample never rises above the top of its
// cells. Samples under the sea (point height below kSeaLevel) never rise, nor do those of level
// 0, which meets the tiles bit for bit.
//
// The points of the cells lie on the fine lattice of level n: node (u, v) at (u, v) * pitch / R,
// sample (i, k) on node (i * R, k * R); each is read at its canonical position, and a sample
// shares the sides of its cells with its neighbors: about 2 (R - 1) + 1 heights more per sample
// on land. A lift is a pure function of the seed, the parameters, the level and the canonical
// index of its sample: the same bits for every image, rectangle, thread and observer. The fine
// lattice may also be read in a HorizonFineGrid (horizon_fine_grid.hpp) of the level at a multiple
// of the ratio, shared with the detail normals (horizon_detail.hpp): the same nodes, the same bits.
class HorizonCrestSampler {
public:
    // field must outlive the sampler and share the world of params. Throws
    // std::invalid_argument for invalid parameters (validate) or another world.
    HorizonCrestSampler(const Heightfield& field, const HorizonParams& params);

    [[nodiscard]] const HorizonParams& params() const { return params_; }
    // Whether the samples of a level may rise: crestRatio > 0 and level >= 1.
    [[nodiscard]] bool raises(int level) const;

    // Lifts of the samples of a rectangle of a level, row by row (x varies fastest), into
    // lifts: 0 where nothing rises, everywhere on a level that does not raise. margined holds
    // the point heights of the rectangle grown by one sample on each side, row by row
    // ((width + 2) x (height + 2)), as Heightfield::heightAt gives them at their canonical
    // positions. Thread-safe. Throws std::invalid_argument for a level out of range or spans of
    // other sizes.
    void fill(int level, const HorizonRect& rect, std::span<const float> margined,
              std::span<float> lifts) const;
    // The same, the point heights computed here.
    [[nodiscard]] std::vector<float> sample(int level, const HorizonRect& rect) const;

    // The nodes of the fine lattice (at the ratio) the lifts of a rectangle may read: those of its
    // cells, [(x0 - 1) * ratio, x1 * ratio + 1) on each axis; empty where the level does not raise.
    [[nodiscard]] HorizonRect nodes(int level, const HorizonRect& rect) const;
    // Reads into a grid of the level, whose ratio is a multiple of this one, what the lifts of a
    // rectangle need and the grid does not hold yet: the nodes of the samples of the rectangle
    // grown by one sample, then those of the sides and centers of the cells with a corner that may
    // rise. The nodes in `held` (indices of the grid) are taken as read. Nothing where the level does not
    // raise. Throws std::invalid_argument for a level out of range, or a grid of another level or
    // ratio, or not covering the nodes.
    void read(int level, const HorizonRect& rect, HorizonFineGrid& grid,
              const HorizonRect& held = {}) const;
    // The lifts of a rectangle (into lifts, as fill above) from a grid holding what read reads.
    void fill(int level, const HorizonRect& rect, const HorizonFineGrid& grid,
              std::span<float> lifts) const;

private:
    [[nodiscard]] int strideIn(int level, const HorizonRect& rect,
                               const HorizonFineGrid& grid) const;

    const Heightfield* field_;
    HorizonParams params_;
};

} // namespace terrain
