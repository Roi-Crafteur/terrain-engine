#pragma once

#include "horizon_textures.hpp"

#include "terrain/streaming/horizon_grids.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <optional>
#include <span>
#include <vector>

namespace terrain::unreal {

// The crests of the GPU horizon computed after the epochs (HorizonGpuParams::deferCrests): the
// epochs write the point samples, then the detail jobs give, for each sample of a band, its point
// height and its height raised to the crests of its cells (HorizonDetailSampler, the same addition
// as HorizonSampler with crests). The texels of the samples that rise are written again, rebuilt
// from the cells of the clipmap as they stand when the band arrives: a band computed before the
// last epoch is still right (a lift only depends on the canonical index of its sample), and a
// sample written again by an epoch is lifted again by the band that epoch queued.
struct CrestBand {
    int level = 0;
    HorizonRect rect;          // samples of the window, row by row below
    std::vector<float> points; // their point heights (heightAt)
    std::vector<float> raised; // their heights raised to the crests (the point where none rises)
};

// The height a cell of the clipmap takes from its crest, or nullopt when it keeps its own. A cell
// rises only while it is the point sample the band read: no player edit (kHorizonEdited unset, the
// edited share s = 0) and its height the point height, bit for bit. An edited cell, partly
// (0 < s < 1) or wholly (s = 1, its height E), keeps the height its epoch gave it, never lifted
// twice nor composed with the edit in another order; a cell already raised keeps its height too.
[[nodiscard]] std::optional<float> crestHeight(const HorizonCell& cell, float point, float raised);

// The texel regions that write the crests of a band into the texture of a level (side samples per
// side of the window): the rows from the first to the last sample that rises, their texels rebuilt
// from the cells of the level (horizonTexel), raised where crestHeight says so. None when nothing
// rises. The band must lie in the window of the level. Throws std::invalid_argument for heights of
// another count than the band.
[[nodiscard]] std::vector<TexelRegion> crestRegions(const HorizonLevel& level, int side,
                                                    const CrestBand& band, bool sea);

// The values of the samples `part` (inside rect) out of the values of rect, one per sample, row by
// row.
[[nodiscard]] std::vector<float> partValues(const HorizonRect& rect, std::span<const float> values,
                                            const HorizonRect& part);

} // namespace terrain::unreal
