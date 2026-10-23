#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_levels.hpp"

#include <span>
#include <vector>

namespace terrain {

// Detail slopes of the far horizon: the slopes of the ground on a lattice `ratio` times finer
// than the samples of each level, for the normal maps a renderer lights its levels with. The
// grid of a level draws the relief at its pitch (8 m to 4 km); its normals at that pitch erase
// the crests, walls and gullies smaller than a few pitches. The detail slopes keep them down to
// about two detail spacings (pitch / ratio), however coarse the grid.
//
// Texel (a, b) of sample (i, k) of level n, 0 <= a, b < ratio, lies at
// (i * pitch + a * spacing, k * pitch + b * spacing), spacing = pitch / ratio: texel (0, 0) is the
// sample itself. Its slopes are those of the Heightfield (heightAt, no player edit) around it,
// prefiltered over its footprint (a Sobel filter on the 3 x 3 heights one spacing apart: the mean
// slope over two spacings along each axis, weighted 1 2 1 across), so that a renderer magnifying
// them never aliases. A pure function of the seed, the level and the canonical position of the
// texel: the same bits for every image, rectangle, thread and observer.
inline constexpr int kHorizonNormalRatioMax = 16;

// Slopes of the ground, rise over run along x and z: the normal is normalize(-gradX, 1, -gradZ).
struct DetailSlope {
    float gradX = 0.0f;
    float gradZ = 0.0f;

    friend bool operator==(const DetailSlope&, const DetailSlope&) = default;
};

class HorizonNormalSampler {
public:
    // field must outlive the sampler and share the world of params. Throws
    // std::invalid_argument for invalid parameters (validate), another world, or a ratio that
    // is not a power of two in [1, kHorizonNormalRatioMax] dividing the pitch of level 0.
    HorizonNormalSampler(const Heightfield& field, const HorizonParams& params, int ratio);

    [[nodiscard]] int ratio() const { return ratio_; }
    [[nodiscard]] const HorizonParams& params() const { return params_; }
    // Meters between the texels of a level: its pitch divided by the ratio.
    [[nodiscard]] int spacing(int level) const;

    // The texels of the samples of a rectangle of a level, rect.width() * ratio by
    // rect.height() * ratio, row by row (x varies fastest): texel (a, b) of sample (i, k) at
    // ((k - z0) * ratio + b) * width * ratio + (i - x0) * ratio + a. Thread-safe. Throws
    // std::invalid_argument for a level out of range or an output of another size.
    void fill(int level, const HorizonRect& rect, std::span<DetailSlope> out) const;
    [[nodiscard]] std::vector<DetailSlope> sample(int level, const HorizonRect& rect) const;

    // Texels of a rectangle of samples.
    [[nodiscard]] std::int64_t texelCount(const HorizonRect& rect) const;

private:
    const Heightfield* field_;
    HorizonParams params_;
    int ratio_;
};

} // namespace terrain
