#include "terrain/generation/cliffs.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/sdf.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace terrain {

namespace {

// Streams of the cliff layers, distinct from the Heightfield, erosion and river streams.
constexpr std::uint64_t kProvinceStream = 0x434C4946; // "CLIF"
constexpr std::uint64_t kRimStream = 0x52494D53;      // "RIMS"
constexpr std::uint64_t kStrataStream = 0x53545241;   // "STRA"

// The footprints f_k are bounded to +-kFootprintBound * densityClamp. The bound must exceed
// (maxTerraceDrop + 2 strata) / maxSlope, so that the fixed window of strata below the
// column gives the same value, once clamped, as an infinite stack of them.
constexpr float kFootprintBound = 4.0f;

// Extra strata kept on each side of the window, so that its ends never show.
constexpr int kStrataMargin = 2;

// Bracketing and bisection of the surface of a column. The bracket normally already holds
// rock at its lower end; the extra steps are a safety net against pathological parameters.
constexpr int kMaxBracketSteps = 8;
constexpr int kBisectionSteps = 40;

// Search for the site of a cliff region: a coarse grid of points every 500 m within 30 km of
// the origin. The whole region must lie in the highlands: above the coast fades, far below
// the summit fade. Its center is preferably in the mid-highlands band, where the rivers have
// relief above them to cascade from and room below them to cut gorges. The altitudes were
// calibrated on the default seed; other seeds fall back to wider windows.
constexpr int kSearchSteps = 60;
constexpr double kSearchSpacing = 500.0;
constexpr float kHighlandLow = 600.0f;
constexpr float kHighlandHigh = 2000.0f;
constexpr float kBandLow = 1200.0f;
constexpr float kBandHigh = 1400.0f;
constexpr int kScorePoints = 5; // per side

// Offset of the strata in [0, stratumHeight), drawn once from the seed.
float drawStratumOffset(std::uint64_t seed, float stratumHeight) {
    const std::uint64_t bits = noise::deriveSeed(seed, kStrataStream);
    const double unit = static_cast<double>(bits >> 40) / 16777216.0; // 2^24 steps in [0, 1)
    return static_cast<float>(static_cast<double>(stratumHeight) * unit);
}

bool hasOverhang(const CliffColumn& column) {
    return column.capOverhang > 0.0f || column.undercut > 0.0f;
}

// Offset P_k(y) of the footprint of the stratum whose top is top: the hard bank (the top
// capShare of the stratum) juts out by capOverhang, the softer rock under it recedes by
// undercut, with a smooth ramp in between. Continuous in y; above the top it keeps the value
// of the bank, where the prism is air anyway.
float overhangOffset(const CliffParams& p, const CliffColumn& column, float top, double y) {
    const auto depth = static_cast<float>((static_cast<double>(top) - y) /
                                          static_cast<double>(p.stratumHeight));
    const float recede = noise::smoothstep(p.capShare, p.capShare + p.capRampShare, depth);
    return -column.capOverhang + (column.capOverhang + column.undercut) * recede;
}

// Strata of a column at height y: vertical prisms stacked every stratumHeight, whose flat
// tops are the terraces. A stratum covers the part of the column where the relief is above
// its top by more than the rim recession. The window of strata depends on the column only,
// not on y, so the density stays continuous in y. Overhangs bend the walls of the prisms;
// without them the footprints are left exactly as they are.
float strataDensity(const CliffField& field, const CliffColumn& column, double y) {
    const CliffParams& p = field.params();
    const bool overhang = hasOverhang(column);
    assert(!overhang || (column.capOverhang >= 0.0f && column.undercut >= 0.0f &&
                         column.capOverhang <= field.maxCapOverhang() &&
                         column.undercut <= field.maxUndercut()));
    const int first = field.stratumBelow(column.ground - field.maxTerraceDrop()) - kStrataMargin;
    const int last = field.stratumBelow(column.ground) + kStrataMargin;
    float strata = std::numeric_limits<float>::max();
    for (int k = first; k <= last; ++k) {
        const float top = field.stratumTop(k);
        float footprint = field.footprint(column, k);
        if (overhang) {
            footprint += overhangOffset(p, column, top, y);
        }
        strata = std::min(strata, sdf::prismBelow(footprint, y, top));
    }
    return std::clamp(strata, -p.densityClamp, p.densityClamp);
}

// Cirques carved below the cascades: the union of vertical cylinders with flat floors, or
// air far away when the column sees none.
float cirqueCarve(const CliffField& field, const CliffColumn& column, double y) {
    float carve = std::numeric_limits<float>::max();
    for (int i = 0; i < column.cirqueCount; ++i) {
        const CirqueCut& cut = column.cirques[static_cast<std::size_t>(i)];
        carve = sdf::unite(carve,
                           sdf::slotAbove(cut.distance, field.params().cirqueRadius, y, cut.floor));
    }
    return carve;
}

// Height under the terraces, the water of the river and the floors of the cirques: below it
// the column is solid for sure.
double lowestFloor(const CliffField& field, const CliffColumn& column) {
    double lowest = static_cast<double>(column.ground) - field.maxTerraceDrop();
    if (column.riverDistance < kNoRiverDistance) {
        lowest = std::min(lowest, static_cast<double>(column.riverLevel));
    }
    for (int i = 0; i < column.cirqueCount; ++i) {
        const CirqueCut& cut = column.cirques[static_cast<std::size_t>(i)];
        lowest = std::min(lowest, static_cast<double>(cut.floor));
    }
    return lowest;
}

struct Site {
    double x = 0.0;
    double z = 0.0;
    float score = 0.0f;
};

// Mean province over a kScorePoints x kScorePoints pattern spanning the region extent, where
// the ground is in the highlands: 1 when the whole region is in a cliff province and above
// the coasts, 0 when none of it is.
float siteScore(const Heightfield& field, const CliffField& cliffs, double x, double z,
                double extent) {
    float sum = 0.0f;
    for (int j = 0; j < kScorePoints; ++j) {
        for (int i = 0; i < kScorePoints; ++i) {
            const double px = x + (i - (kScorePoints - 1) / 2.0) / (kScorePoints - 1) * extent;
            const double pz = z + (j - (kScorePoints - 1) / 2.0) / (kScorePoints - 1) * extent;
            const float height = field.heightAt(px, pz);
            if (height >= kHighlandLow && height <= kHighlandHigh) {
                sum += cliffs.provinceAt(px, pz);
            }
        }
    }
    return sum / static_cast<float>(kScorePoints * kScorePoints);
}

// Best site whose center altitude is within [low, high]; the score stays 0 when none scores.
Site searchSite(const Heightfield& field, const CliffField& cliffs, double extent, float low,
                float high) {
    Site best;
    for (int j = -kSearchSteps; j <= kSearchSteps; ++j) {
        for (int i = -kSearchSteps; i <= kSearchSteps; ++i) {
            const double x = i * kSearchSpacing;
            const double z = j * kSearchSpacing;
            const float height = field.heightAt(x, z);
            if (height < low || height > high) {
                continue;
            }
            const float score = siteScore(field, cliffs, x, z, extent);
            if (score > best.score) { // strict: ties keep the first site in loop order
                best = {x, z, score};
            }
        }
    }
    return best;
}

} // namespace

CliffField::CliffField(std::uint64_t seed, const CliffParams& params, const WorldSize& world)
    : params_(params), world_(world), provinceSeed_(noise::deriveSeed(seed, kProvinceStream)),
      rimSeed_(noise::deriveSeed(seed, kRimStream)),
      stratumOffset_(drawStratumOffset(seed, params.stratumHeight)) {
    validate(world_);
}

float CliffField::provinceAt(double x, double z) const {
    const noise::FbmParams fbm{.octaves = 3, .frequency = 1.0 / params_.provinceWavelength};
    return noise::smoothstep(params_.provinceLow, params_.provinceHigh,
                             noise::fbm2D(x, z, provinceSeed_, fbm, world_));
}

float CliffField::maskAt(double x, double z, float ground, float borderWeight) const {
    // Fades first: most of the world needs no noise at all.
    if (borderWeight <= 0.0f || ground <= params_.coastFadeLow ||
        ground >= params_.summitFadeHigh) {
        return 0.0f;
    }
    const float coast = noise::smoothstep(params_.coastFadeLow, params_.coastFadeHigh, ground);
    const float summit =
        1.0f - noise::smoothstep(params_.summitFadeLow, params_.summitFadeHigh, ground);
    return provinceAt(x, z) * coast * summit * borderWeight;
}

float CliffField::rimRecessionAt(double x, double z) const {
    const noise::FbmParams fbm{.octaves = 3, .frequency = 1.0 / params_.rimWavelength};
    return params_.rimRecession * (0.5f + 0.5f * noise::fbm2D(x, z, rimSeed_, fbm, world_));
}

float CliffField::stratumTop(int k) const {
    return static_cast<float>(k * static_cast<double>(params_.stratumHeight) +
                              static_cast<double>(stratumOffset_));
}

int CliffField::stratumBelow(float height) const {
    const double scaled = (static_cast<double>(height) - static_cast<double>(stratumOffset_)) /
                          static_cast<double>(params_.stratumHeight);
    int k = static_cast<int>(std::floor(scaled));
    // Rounding of the float tops must not break "highest top at or below height".
    while (stratumTop(k + 1) <= height) {
        ++k;
    }
    while (stratumTop(k) > height) {
        --k;
    }
    return k;
}

float CliffField::footprint(const CliffColumn& column, int k) const {
    // Height difference to horizontal distance through the local slope.
    const float slope = std::clamp(column.slope, params_.minSlope, params_.maxSlope);
    const float bound = kFootprintBound * params_.densityClamp;
    return std::clamp((stratumTop(k) - column.ground) / slope, -bound, bound) +
           column.rimRecession;
}

float CliffField::maxTerraceDrop() const {
    return params_.stratumHeight + params_.rimRecession * params_.maxSlope;
}

float CliffField::maxCapOverhang() const {
    // The lowest footprint of a stratum above the window, minus the bank, must stay above the
    // clamp of the density.
    const float bound = kFootprintBound * params_.densityClamp;
    const float above = kStrataMargin * params_.stratumHeight / params_.maxSlope;
    return std::min(above, bound) - params_.densityClamp;
}

float CliffField::maxUndercut() const {
    // The highest footprint of the lowest stratum of the window, plus the undercut, must stay
    // below the clamp of the density.
    const float bound = kFootprintBound * params_.densityClamp;
    const float below =
        (maxTerraceDrop() + kStrataMargin * params_.stratumHeight) / params_.maxSlope;
    return std::min(below, bound) - params_.rimRecession - params_.densityClamp;
}

float CliffField::density(const CliffColumn& column, double y) const {
    // Exactly the relief where the cliffs are off, so that the rest of the world is unchanged.
    if (column.cliff <= 0.0f) {
        return static_cast<float>(y - column.ground);
    }
    const double ground = column.ground;
    const double mask = column.cliff;
    const float strata = strataDensity(*this, column, y);
    // Within riverReach of a river the rock never ends below the water.
    const float riverFloor = std::max(
        static_cast<float>(y - std::min(column.riverLevel, column.ground)),
        column.riverDistance - params_.riverReach);
    const float rock = sdf::unite(strata, riverFloor);
    const float gorge =
        sdf::slotAbove(column.riverDistance, column.gorgeHalfWidth, y, column.riverLevel);
    const float cliff = sdf::subtract(rock, gorge);
    const auto blended =
        static_cast<float>((1.0 - mask) * (y - ground) + mask * static_cast<double>(cliff));
    // The cirques are carved at full strength, after the blend, so that their walls stay
    // vertical where the mask is below 1. They are only placed where the mask is positive all
    // around them (see CliffRegion), so the density stays continuous.
    if (column.cirqueCount == 0) {
        return blended;
    }
    return sdf::subtract(blended, cirqueCarve(*this, column, y));
}

float CliffField::topSurface(const CliffColumn& column) const {
    if (column.cliff <= 0.0f) {
        return column.ground;
    }
    // Every term of the density grows with y: one surface, found by bisection between air
    // just above the relief and rock below the lowest terrace, river or cirque floor.
    double hi = static_cast<double>(column.ground) + 1.0;
    double lo = lowestFloor(*this, column) - 1.0;
    for (int i = 0; i < kMaxBracketSteps && density(column, lo) >= 0.0f; ++i) {
        lo -= 2.0 * (hi - lo);
    }
    for (int i = 0; i < kBisectionSteps; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (density(column, mid) < 0.0f) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return static_cast<float>(0.5 * (lo + hi));
}

HeightmapRegion findCliffRegion(std::uint64_t seed, double metersPerPixel, int size,
                                const CliffParams& params) {
    return findCliffRegion(seed, metersPerPixel, size, params, HeightfieldParams{});
}

HeightmapRegion findCliffRegion(std::uint64_t seed, double metersPerPixel, int size,
                                const CliffParams& params, const HeightfieldParams& heightfield) {
    const Heightfield field(seed, heightfield);
    const CliffField cliffs(seed, params);
    const double extent = size * metersPerPixel;
    // Narrow band first, then any highland, then any altitude.
    Site site = searchSite(field, cliffs, extent, kBandLow, kBandHigh);
    if (site.score <= 0.0f) {
        site = searchSite(field, cliffs, extent, kHighlandLow, kHighlandHigh);
    }
    if (site.score <= 0.0f) {
        site = searchSite(field, cliffs, extent, std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::max());
    }
    // Snap to the chunk grid, like the other regions, so chunks line up with the region.
    const double chunk = kChunkSizeMeters;
    return {std::round(site.x / chunk) * chunk, std::round(site.z / chunk) * chunk,
            metersPerPixel, size};
}

} // namespace terrain
