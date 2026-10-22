#pragma once

// The boundaries of the tectonic plates of the geography and the noises along them, internal to
// terrain_core: what each boundary draws (its kind, width, plateau, rift), the warp that bends
// the boundaries, and the noises that vary the belts along them. geography_tectonics.cpp turns
// them into the fields of the nodes of the map.

#include "geography_stages.hpp"
#include "terrain/generation/voronoi.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain::geography {

// What the geography draws along a boundary of the plates.
struct Boundary {
    BoundaryKind kind = BoundaryKind::Transform;
    float strength = 0.0f;     // kWeakest to 1, from the convergence
    double halfWidth = 0.0;    // of the belt, before its spread
    int overriding = 0;        // the plate the belt leans toward, the plateau lies on
    double plateauWidth = 0.0; // 0 without plateau
    bool chainless = false;    // a collision of foothills only, without a chain
    bool rift = false;         // a divergence that opens a continental rift
    float riftDepth = 0.0f;    // meters of its floor below its shoulders
    double reach = 0.0;        // no effect farther than this from the bisector
};

// The convergence above which (below whose opposite) a boundary is a collision (a divergence),
// and the weakest strength of its effects.
inline constexpr double kConvergent = 0.3;
inline constexpr double kWeakest = 0.35;

// Along a boundary, the width and uplift of a belt vary by +-kWidthSpread and +-kUpliftSpread,
// the extent of a plateau by +-kPlateauExtent; its axis meanders by up to kMeanderLarge and
// kMeanderSmall meters.
inline constexpr double kWidthSpread = 0.3;
inline constexpr double kUpliftSpread = 0.35;
inline constexpr double kPlateauSpread = 0.1;
inline constexpr double kPlateauExtent = 0.4;
inline constexpr double kMeanderLarge = 110'000.0;
inline constexpr double kMeanderSmall = 40'000.0;

// Continental rifts open along a share of the divergences above kRiftDivergence, their shoulders
// kShoulders rift widths wide.
inline constexpr double kRiftDivergence = 0.6;
inline constexpr double kShoulders = 2.5;

// The shortest offset on a torus between two coordinates of its canonical range.
[[nodiscard]] inline double shortOffset(double delta, double period) {
    if (delta > 0.5 * period) {
        return delta - period;
    }
    return delta < -0.5 * period ? delta + period : delta;
}

// Between its ranges a belt falls to kGap of itself, where the noise of its ranges goes down
// from kGapHigh to kGapLow: a pass or a gap, reached gradually (over more than 180 km on 95 % of
// the ends of the ranges; the tails of the belts soften the others), about 10 % of the length.
inline constexpr float kGap = 0.05f;
inline constexpr double kGapLow = -0.6;
inline constexpr double kGapHigh = 0.3;

// The share of a belt kept along its ranges at the noise of its ranges (NodeFrame::ranges).
[[nodiscard]] inline float rangesFactor(double ranges) {
    const double t = std::clamp((ranges - kGapLow) / (kGapHigh - kGapLow), 0.0, 1.0);
    return kGap + (1.0f - kGap) * static_cast<float>(t * t * (3.0 - 2.0 * t));
}

// The share of land of each plate (the land of the nodes nearest to it after the warp): the plate
// with more land overrides the other in a collision.
[[nodiscard]] std::vector<double> landShares(std::size_t plates, const std::vector<int>& nearest,
                                             const std::vector<std::uint8_t>& land);

// Every boundary of the plates, drawn from the seed in their order.
[[nodiscard]] std::vector<Boundary> drawBoundaries(std::uint64_t seed, const GeographyParams& p,
                                                   const TectonicPlates& plates,
                                                   const std::vector<double>& landShare);

// The noises along the boundaries at a node, in [-1, 1], and its warped position (where the
// plates are looked up).
struct NodeFrame {
    TectonicPlates::Point warped;
    double width = 0.0;
    double uplift = 0.0;
    double ranges = 0.0;
    double meander = 0.0; // meters
    double extent = 0.0;  // of the plateaus
    double edge = 0.0;    // the jagged far edge of the plateaus
};

// The slow fields of the plates on a grid kSlowCoarse times coarser than the map, read
// bilinearly at its nodes (the warp of the plates as an offset, and the slowest noises along the
// boundaries), the nearest plate and the land of each coarse node; and the faster noises, read
// at the nodes themselves: the warp that bends the boundaries between 200 and 1,000 km, the
// meander of the axes, the plateaus.
class BoundaryFields {
public:
    BoundaryFields(std::uint64_t stream, const TectonicPlates& plates, const Grid& grid,
                   const Crust& crust, int threads);

    // The fields of node (i, j) of the map.
    [[nodiscard]] NodeFrame at(int i, int j) const;
    // The nearest plate and the land of each coarse node (landShares).
    [[nodiscard]] const std::vector<int>& nearest() const { return nearest_; }
    [[nodiscard]] const std::vector<std::uint8_t>& land() const { return land_; }

    struct Noises;

private:
    const Grid& grid_;
    Grid coarse_;
    std::vector<std::array<double, 5>> slow_;
    std::vector<int> nearest_;
    std::vector<std::uint8_t> land_;
    std::shared_ptr<const Noises> noises_;
};

} // namespace terrain::geography
