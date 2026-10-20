#include "terrain/generation/sculpted_region.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace terrain {

namespace {

// Largest step of the downward search of the surfaces of a column, in meters: the thinnest
// rock (the bank of a stratum, the tube of an arch) and air (a notch, an opening) are several
// meters thick.
constexpr double kScanStep = 1.0;
constexpr int kBisectionSteps = 30;

bool isPlain(const SculptedColumn& column) {
    return column.overhang <= 0.0f && column.arch == kNoArch;
}

} // namespace

SculptedRegion::SculptedRegion(std::uint64_t seed, const HeightmapRegion& region,
                               const SculptedRegionParams& params, const WorldSize& world)
    : cliffs_(seed, region, params.cliffs, world), overhangs_(seed, params.overhangs, world),
      archField_(seed, params.arches, world), archFade_(params.archFade),
      surface_(cliffs_.surface()) {
    arches_ = placeArches(cliffs_, archField_);
    computeGrids();
}

float SculptedRegion::controlAt(double x, double z, const CliffColumn& column) const {
    float control = overhangs_.controlAt(x, z, column, cliffs_.field().params());
    for (const Arch& arch : arches_) {
        if (control <= 0.0f) {
            break;
        }
        const double reach = archField_.reach(arch);
        const double distance = dmath::hypot(x - arch.x, z - arch.z);
        control *= noise::smoothstep(static_cast<float>(reach),
                                     static_cast<float>(reach + archFade_),
                                     static_cast<float>(distance));
    }
    return control;
}

SculptedColumn SculptedRegion::columnAt(double x, double z) const {
    SculptedColumn column{x, z, cliffs_.columnAt(x, z)};
    for (std::size_t i = 0; i < arches_.size(); ++i) {
        const Arch& arch = arches_[i];
        if (dmath::hypot(x - arch.x, z - arch.z) < archField_.reach(arch)) {
            column.arch = static_cast<int>(i); // arches are kept out of each other's reach
        }
    }
    const float control = controlAt(x, z, column.cliff);
    if (control > 0.0f) {
        const ReliefHeight relief = [this](double hx, double hz) {
            return cliffs_.eroded().heightAt(hx, hz);
        };
        column.overhang =
            overhangs_.weightAt(cliffs_.field(), column.cliff, x, z, control, relief);
        overhangs_.apply(column.cliff, column.overhang);
    }
    return column;
}

float SculptedRegion::density(const SculptedColumn& column, double y) const {
    const float cliff = cliffs_.field().density(column.cliff, y);
    if (column.arch == kNoArch) {
        return cliff;
    }
    return archField_.apply(arches_[static_cast<std::size_t>(column.arch)], cliff, column.x, y,
                            column.z);
}

float SculptedRegion::densityAt(double x, double y, double z) const {
    return density(columnAt(x, z), y);
}

SculptedRegion::Bounds SculptedRegion::bounds(const SculptedColumn& column) const {
    const CliffField& field = cliffs_.field();
    CliffColumn plain = column.cliff;
    plain.capOverhang = 0.0f;
    plain.undercut = 0.0f;
    const float top = field.topSurface(plain);
    Bounds result{top - 1.0f, top + 1.0f};
    if (column.overhang > 0.0f) {
        // A notch reaches down to the tread of the stratum below; a bank juts out at most
        // capOverhang * slope above the relief (and the cliffs never rise above it).
        result.low = top - field.params().stratumHeight - 1.0f;
        result.high = std::max(top, column.cliff.ground +
                                        column.cliff.capOverhang * field.params().maxSlope) +
                      1.0f;
    }
    if (column.arch != kNoArch) {
        // The opening is carved down to the base, the arch rises to its crown.
        const Arch& arch = arches_[static_cast<std::size_t>(column.arch)];
        result.low = std::min(result.low, arch.base - 1.0f);
        result.high = std::max(result.high, archField_.crown(arch) + 1.0f);
    }
    return result;
}

SculptedRegion::Scan SculptedRegion::scan(const SculptedColumn& column) const {
    if (isPlain(column)) {
        return {cliffs_.field().topSurface(column.cliff), false};
    }
    const Bounds range = bounds(column);
    const double high = range.high;
    const double low = range.low;
    const int steps = std::max(1, static_cast<int>(std::ceil((high - low) / kScanStep)));
    const auto heightOf = [&](int step) { return high - (high - low) * step / steps; };
    int first = steps; // first step in rock, going down
    for (int step = 1; step < steps; ++step) {
        if (density(column, heightOf(step)) < 0.0f) {
            first = step;
            break;
        }
    }
    double air = heightOf(first - 1);
    double rock = heightOf(first);
    for (int i = 0; i < kBisectionSteps; ++i) {
        const double mid = 0.5 * (air + rock);
        if (density(column, mid) < 0.0f) {
            rock = mid;
        } else {
            air = mid;
        }
    }
    Scan result{static_cast<float>(0.5 * (air + rock)), false};
    for (int step = first + 1; step < steps && !result.airBelowRock; ++step) {
        result.airBelowRock = density(column, heightOf(step)) > 0.0f;
    }
    return result;
}

float SculptedRegion::surfaceAt(double x, double z) const {
    return scan(columnAt(x, z)).top;
}

void SculptedRegion::computeGrids() {
    const ErodedRegion& eroded = cliffs_.eroded();
    const std::size_t count = surface_.heights.size();
    control_.assign(count, 0.0f);
    weight_.assign(count, 0.0f);
    airBelowRock_.assign(count, 0);
    for (int row = 0; row < surface_.size; ++row) {
        for (int column = 0; column < surface_.size; ++column) {
            const std::size_t index = surface_.index(column, row);
            const SculptedColumn at = columnAt(eroded.sampleX(column), eroded.sampleZ(row));
            control_[index] = controlAt(at.x, at.z, at.cliff);
            weight_[index] = at.overhang;
            if (!isPlain(at)) { // plain columns keep the surface of the cliff region
                const Scan result = scan(at);
                surface_.heights[index] = result.top;
                airBelowRock_[index] = result.airBelowRock ? 1 : 0;
            }
        }
    }
}

SculptedChunkColumns sampleSculptedColumns(const SculptedRegion& region, double originX,
                                           double originZ, double spacing) {
    SculptedChunkColumns result;
    result.spacing = spacing;
    result.columns.reserve(static_cast<std::size_t>(kGridSamples * kGridSamples));
    result.minHeight = std::numeric_limits<float>::max();
    result.maxHeight = std::numeric_limits<float>::lowest();
    for (int z = 0; z < kGridSamples; ++z) {
        for (int x = 0; x < kGridSamples; ++x) {
            result.columns.push_back(region.columnAt(originX + x * spacing, originZ + z * spacing));
            const SculptedRegion::Bounds bounds = region.bounds(result.columns.back());
            result.minHeight = std::min(result.minHeight, bounds.low);
            result.maxHeight = std::max(result.maxHeight, bounds.high);
        }
    }
    return result;
}

DensityGrid sculptedDensityFromColumns(const SculptedRegion& region,
                                       const SculptedChunkColumns& columns, double originY) {
    DensityGrid grid(0.0f);
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            const double sampleY = originY + y * columns.spacing;
            for (int x = 0; x < kGridSamples; ++x) {
                grid.set(x, y, z, region.density(columns.at(x, z), sampleY));
            }
        }
    }
    return grid;
}

} // namespace terrain
