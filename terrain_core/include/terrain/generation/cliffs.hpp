#pragma once

#include "terrain/generation/height_grid.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain {

// Lengths and heights are in meters. The cliffs are a 3D density volume laid on an eroded
// relief H(x, z): density < 0 is rock, 0 the surface, > 0 air. They only remove rock from
// the relief, so mountains, coasts and oceans keep their shape.
struct CliffParams {
    // Cliff mask M = province * coast fade * summit fade * region border weight, in [0, 1].
    // Province: smoothstep(provinceLow, provinceHigh, fbm) with 3 octaves.
    double provinceWavelength = 8000.0;
    float provinceLow = 0.0f;
    float provinceHigh = 0.1f;
    float coastFadeLow = 80.0f;     // M = 0 below this altitude (coasts stay untouched)
    float coastFadeHigh = 160.0f;   // full above it
    float summitFadeLow = 2600.0f;  // full below this altitude
    float summitFadeHigh = 3200.0f; // M = 0 above it (summits stay untouched)
    int borderMargin = 16;          // cells: M fades in from the region border ring

    // Strata: rock layers whose flat tops are at T_k = k * stratumHeight + offset, with the
    // offset in [0, stratumHeight) drawn from the seed.
    float stratumHeight = 40.0f;
    // Plateau rims recede from the contour line by fbm noise mapped to [0, rimRecession].
    double rimWavelength = 400.0;
    float rimRecession = 15.0f;
    // Range of |grad H| used to turn a height difference into a horizontal distance.
    float minSlope = 0.05f;
    float maxSlope = 2.0f;
    // The strata density is bounded to +-densityClamp, which keeps it continuous.
    float densityClamp = 24.0f;
    // Overhangs (only where a column asks for them, see CliffColumn): the top capShare of
    // each stratum is a hard bank, which may jut out over the softer rock below it, which may
    // recede. The profile switches from one to the other over capRampShare of the stratum.
    float capShare = 0.3f;
    float capRampShare = 0.1f;

    // Rivers: within riverReach of a river, the strata never cut below its water.
    float riverReach = 120.0f;
    // Gorges along the rivers draining at least gorgeMinCatchment (m^2, twice the river
    // threshold): vertical walls, floor at the water, half-width growing to
    // gorgeMaxHalfWidth once the catchment is 2^gorgeWidthDoublings times larger.
    double gorgeMinCatchment = 4.0e6;
    float gorgeMaxHalfWidth = 70.0f;
    double gorgeWidthDoublings = 2.0;

    // Cirque (reculee) carved just below each cascade where M >= cirqueMinMask and M > 0 all
    // around: a vertical cylinder of cirqueRadius whose floor is the water of the cascade's
    // receiver, so the waterfall drops from a vertical wall.
    float cirqueRadius = 60.0f;
    float cirqueMinMask = 0.5f;
};

// Distance of a column with no river nearby; also caps the river distances.
inline constexpr float kNoRiverDistance = 1.0e4f;

// One cirque seen from a column.
struct CirqueCut {
    float distance = 0.0f; // horizontal distance from the column to the cirque axis
    float floor = 0.0f;    // height of the cirque floor
};

inline constexpr int kMaxColumnCirques = 2;

// Everything the cliff density of a column (x, z) depends on, apart from the height y.
struct CliffColumn {
    float ground = 0.0f;       // eroded height H
    float slope = 0.0f;        // |grad H|, clamped to [minSlope, maxSlope]
    float cliff = 0.0f;        // mask M; 0 gives exactly y - ground
    float rimRecession = 0.0f; // in [0, rimRecession]
    float riverDistance = kNoRiverDistance; // to the nearest river cell
    float riverLevel = 0.0f;   // water height of the nearest river
    float gorgeHalfWidth = 0.0f; // 0 away from gorges
    std::array<CirqueCut, kMaxColumnCirques> cirques{};
    int cirqueCount = 0;
    // Overhangs of the strata, in meters (0: none, and always 0 in CliffRegion): the hard
    // bank of each stratum juts out by capOverhang, the rock under it recedes by undercut.
    // Bounded by maxCapOverhang() and maxUndercut().
    float capOverhang = 0.0f;
    float undercut = 0.0f;
};

// Deterministic noise layers and density of the cliffs: the same seed, parameters and
// column always give the same density.
class CliffField {
public:
    // Periodic on the world (WorldSize). Throws std::invalid_argument for an invalid world.
    explicit CliffField(std::uint64_t seed, const CliffParams& params = {},
                        const WorldSize& world = {});

    // Province factor of the mask at (x, z), in [0, 1].
    [[nodiscard]] float provinceAt(double x, double z) const;
    // Cliff mask M from the province, the eroded height and the region border weight.
    [[nodiscard]] float maskAt(double x, double z, float ground, float borderWeight) const;
    // Rim recession at (x, z), in [0, rimRecession].
    [[nodiscard]] float rimRecessionAt(double x, double z) const;

    // Top of stratum k, and the highest stratum whose top is at or below height.
    [[nodiscard]] float stratumTop(int k) const;
    [[nodiscard]] int stratumBelow(float height) const;

    // Footprint f_k of stratum k at a column, overhangs aside: the signed horizontal distance
    // to the outline of the stratum (negative inside), clamp((T_k - H) / slope, -F, F) + rim.
    [[nodiscard]] float footprint(const CliffColumn& column, int k) const;

    // Density at height y of a column:
    //   D = max((1 - M) * (y - H) + M * max(min(strata, riverFloor), -gorge), -cirques)
    //   strata     = clamp(min_k max(f_k + P_k(y), y - T_k), -densityClamp, densityClamp)
    //   f_k        = clamp((T_k - H) / slope, -F, F) + rimRecession  (F: internal bound)
    //   P_k(y)     = -capOverhang + (capOverhang + undercut) * smoothstep(capShare,
    //                capShare + capRampShare, (T_k - y) / stratumHeight): 0 without overhangs
    //   riverFloor = max(y - min(riverLevel, H), riverDistance - riverReach)
    //   gorge      = sdf::slotAbove(riverDistance, gorgeHalfWidth, y, riverLevel)
    //   cirques    = min of the cirque slots: carved at full strength, so their walls stay
    //                vertical where M < 1
    // With M = 0 it returns exactly y - ground. Without overhangs it never decreases with y,
    // so a column has a single surface; with overhangs, rock may lie above air.
    [[nodiscard]] float density(const CliffColumn& column, double y) const;

    // Height of the surface of a column (the top of the rock), found by bisection. Only valid
    // for a column without overhangs (a single surface).
    [[nodiscard]] float topSurface(const CliffColumn& column) const;

    // Largest drop of the strata below the eroded height:
    // stratumHeight + rimRecession * maxSlope.
    [[nodiscard]] float maxTerraceDrop() const;

    // Largest overhangs the fixed window of strata of a column supports: beyond them a stratum
    // outside the window could show. capOverhang < 2 * stratumHeight / maxSlope - densityClamp
    // and undercut < (maxTerraceDrop + 2 * stratumHeight) / maxSlope - rimRecession -
    // densityClamp (16 m and 36 m with the default parameters).
    [[nodiscard]] float maxCapOverhang() const;
    [[nodiscard]] float maxUndercut() const;

    [[nodiscard]] const CliffParams& params() const { return params_; }
    [[nodiscard]] const WorldSize& world() const { return world_; }

private:
    CliffParams params_;
    WorldSize world_;
    std::uint64_t provinceSeed_;
    std::uint64_t rimSeed_;
    float stratumOffset_;
};

// Square region centered on a deterministic site of the world with cliffs and rivers: the
// best site of a coarse search around the origin (inside a cliff province, in highlands
// below the summit fade).
[[nodiscard]] HeightmapRegion findCliffRegion(std::uint64_t seed, double metersPerPixel,
                                              int size, const CliffParams& params = {});

} // namespace terrain
