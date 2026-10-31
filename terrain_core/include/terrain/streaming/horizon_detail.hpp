#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_crests.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_normals.hpp"

#include <optional>
#include <vector>

namespace terrain {

// The detail of a rectangle of a level of the far horizon, computed after its samples: the detail
// slopes of its texels (HorizonNormalSampler at the normal ratio, none at 0) and the lifts of its
// samples to the crests of their cells (HorizonCrestSampler at HorizonParams::crestRatio, from
// level 1 on), with their point heights.
//
// When the crests are sought at most as finely as the normals (the defaults: crests at 2 or 4,
// normals at 4), one HorizonFineGrid of the level at the normal ratio is read once: the nodes of
// the normals, densely, then only the nodes the crests add around them (the samples and the sides
// and centers of the cells along the low edges of the rectangle). Otherwise each reads its own
// lattice. Either way the slopes and the lifts are those of each sampler alone, bit for bit: pure
// functions of the seed, the parameters, the level and the canonical indices.
struct HorizonDetail {
    // As HorizonNormalSampler::fill lays them out; empty without normals.
    std::vector<DetailSlope> slopes;
    // Heights of the samples (Heightfield::heightAt, no player edit) and their lifts
    // (HorizonCrestSampler::fill), row by row; both empty where the level does not raise.
    std::vector<float> points;
    std::vector<float> lifts;
};

class HorizonDetailSampler {
public:
    // field must outlive the sampler and share the world of params. normalRatio 0: no slope;
    // params.crestRatio 0: no lift. Throws std::invalid_argument for invalid parameters, another
    // world, or a normal ratio HorizonNormalSampler refuses.
    HorizonDetailSampler(const Heightfield& field, const HorizonParams& params, int normalRatio);

    [[nodiscard]] const HorizonParams& params() const { return crests_.params(); }
    [[nodiscard]] int normalRatio() const { return normals_ ? normals_->ratio() : 0; }
    [[nodiscard]] const HorizonNormalSampler* normals() const {
        return normals_ ? &*normals_ : nullptr;
    }
    [[nodiscard]] const HorizonCrestSampler& crests() const { return crests_; }

    // Whether a level has some detail: slopes, or lifts (from level 1 on).
    [[nodiscard]] bool hasDetail(int level) const;
    // Whether the slopes and the lifts of a level come from one grid.
    [[nodiscard]] bool shared(int level) const;

    // The detail of a rectangle of a level. Thread-safe. Throws std::invalid_argument for a level
    // out of range.
    [[nodiscard]] HorizonDetail sample(int level, const HorizonRect& rect) const;

private:
    void sampleShared(int level, const HorizonRect& rect, HorizonDetail& detail) const;
    void sampleLifts(int level, const HorizonRect& rect, HorizonDetail& detail) const;

    const Heightfield* field_;
    std::optional<HorizonNormalSampler> normals_;
    HorizonCrestSampler crests_;
};

} // namespace terrain
