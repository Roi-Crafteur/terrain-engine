#include "terrain/generation/overhangs.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain {

namespace {

// Stream of the overhang patches, distinct from the cliff and arch streams.
constexpr std::uint64_t kPatchStream = 0x4F564847; // "OVHG"

// Strata kept on each side of the window of a column, as in the cliff density.
constexpr int kStrataMargin = 2;

// Unit directions of the support probes, every 45 degrees.
constexpr double kDiagonal = 0.70710678118654752;
constexpr std::array<std::array<double, 2>, 8> kProbeDirections = {{{1.0, 0.0},
                                                                   {kDiagonal, kDiagonal},
                                                                   {0.0, 1.0},
                                                                   {-kDiagonal, kDiagonal},
                                                                   {-1.0, 0.0},
                                                                   {-kDiagonal, -kDiagonal},
                                                                   {0.0, -1.0},
                                                                   {kDiagonal, -kDiagonal}}};

} // namespace

OverhangField::OverhangField(std::uint64_t seed, const OverhangParams& params)
    : params_(params), patchSeed_(noise::deriveSeed(seed, kPatchStream)) {}

float OverhangField::patchAt(double x, double z) const {
    const noise::FbmParams fbm{.octaves = 3, .frequency = 1.0 / params_.patchWavelength};
    return noise::smoothstep(params_.patchLow, params_.patchHigh,
                             noise::fbm2D(x, z, patchSeed_, fbm));
}

float OverhangField::controlAt(double x, double z, const CliffColumn& column,
                               const CliffParams& cliffs) const {
    // Cheap factors first: most columns need no noise at all.
    if (column.cliff <= params_.minCliff) {
        return 0.0f;
    }
    const float cliff = noise::smoothstep(params_.minCliff, 1.0f, column.cliff);
    const float steep = noise::smoothstep(params_.minSlope, params_.fullSlope, column.slope);
    const float river = noise::smoothstep(params_.riverFadeStart * cliffs.riverReach,
                                          cliffs.riverReach, column.riverDistance);
    if (steep <= 0.0f || river <= 0.0f) {
        return 0.0f;
    }
    return patchAt(x, z) * cliff * steep * river;
}

float OverhangField::zoneWeight(float footprint) const {
    const float margin = params_.zoneMargin;
    const float low = -params_.undercut - margin;
    const float high = params_.capOverhang + margin;
    return noise::smoothstep(low - margin, low, footprint) *
           (1.0f - noise::smoothstep(high, high + margin, footprint));
}

float OverhangField::supportWeight(float depth) const {
    const float threshold = params_.undercut + params_.supportMargin;
    return noise::smoothstep(threshold, threshold + params_.supportMargin, depth);
}

float OverhangField::supportDepth(const CliffField& cliffs, const CliffColumn& column, int k,
                                  double x, double z, const ReliefHeight& relief) const {
    float deepest = -cliffs.footprint(column, k);
    CliffColumn probe = column;
    for (const double radius : {params_.innerProbeRadius, params_.outerProbeRadius}) {
        for (const auto& direction : kProbeDirections) {
            probe.ground = relief(x + radius * direction[0], z + radius * direction[1]);
            deepest = std::max(deepest, -cliffs.footprint(probe, k));
        }
    }
    return deepest;
}

float OverhangField::weightAt(const CliffField& cliffs, const CliffColumn& column, double x,
                              double z, float control, const ReliefHeight& relief) const {
    if (control <= 0.0f) {
        return 0.0f;
    }
    // The same window of strata as the cliff density: the strata beyond it are far from
    // their walls.
    const int first = cliffs.stratumBelow(column.ground - cliffs.maxTerraceDrop()) - kStrataMargin;
    const int last = cliffs.stratumBelow(column.ground) + kStrataMargin;
    float best = 0.0f;
    for (int k = first; k <= last; ++k) {
        const float zone = zoneWeight(cliffs.footprint(column, k));
        if (zone > best) { // otherwise zone * support cannot beat best
            const float depth = supportDepth(cliffs, column, k, x, z, relief);
            best = std::max(best, zone * supportWeight(depth));
        }
    }
    return control * best;
}

void OverhangField::apply(CliffColumn& column, float weight) const {
    if (weight > 0.0f) {
        column.capOverhang = weight * params_.capOverhang;
        column.undercut = weight * params_.undercut;
    }
}

} // namespace terrain
