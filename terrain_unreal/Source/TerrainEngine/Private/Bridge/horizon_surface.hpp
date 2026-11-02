#pragma once

#include "horizon_gpu_streamer.hpp"

#include <optional>

namespace terrain::unreal {

// Where the materials of the tiles read the surface of level 0 of the GPU horizon (SurfaceOrigin,
// SurfaceFirstX/Z and HasSurface, UTerrainHorizonClipmap): over the band of the tiles over
// level 0, the surface blend of the tiles fades into its samples and their water into the water
// of the horizon (horizonWaterShare). Set while level 0 is shown, none while it is hidden: a new
// place (the start, a teleport) is brought level by level (HorizonGpuParams::progressive), every
// level hidden at once, and level 0 shown again only when its first epoch there is committed
// (the pieces of the old place still in flight ended, then its samples computed: seconds). Until
// then the window of the old place no longer holds the tiles: read there, it would leave their
// water to a horizon that draws none (and their ground to texels of the old place).
struct SurfaceWindow {
    UnrealPoint origin;     // low corner of the window of level 0 (LevelPlacement::origin)
    HorizonIndex firstSlot; // texel of its sample at that corner

    friend bool operator==(const SurfaceWindow&, const SurfaceWindow&) = default;
};

// The surface once placement is applied: level 0 shown gives its window, level 0 hidden none,
// another level leaves surface as it is.
[[nodiscard]] std::optional<SurfaceWindow> surfaceAfter(const std::optional<SurfaceWindow>& surface,
                                                        const LevelPlacement& placement);

// The share of the water of a point of the tiles left to the GPU horizon, as the material of the
// water of the tiles computes it (Scripts/water_look_hlsl.py, FTerrainWater::HorizonShare under
// HasSurface and InWindow): this is its reference. relX, relY: centimeters from the low corner
// of level 0 (Unreal X and Y); cells of pitchCm in its window; coveredCm from its center the
// area the tiles always cover, bandCm the band of the tiles over level 0. None without a surface
// placed, nor outside the window of level 0 (its water and samples hold only there: the tiles
// keep theirs); 0 up to coveredCm - bandCm / 2 from its center, 1 from coveredCm on, linear
// between (the horizon dithers its water in there).
[[nodiscard]] double horizonWaterShare(bool hasSurface, double relX, double relY, double cells,
                                       double pitchCm, double coveredCm, double bandCm);

} // namespace terrain::unreal
