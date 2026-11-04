#include "sea_hills.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::sea_hills {
namespace {

// The plane of the frames: 2^27 m on each side, far beyond the coordinates of any frame (at most
// a world, 6e7 m) and a multiple of kWorldAlignmentMeters.
constexpr double kPlane = 134'217'728.0;
// The streams of the noises and draws of the seas.
enum Stream : std::uint64_t {
    kHills = 0,
    kAmplitude = 8,
    kMargins = 9,
    kPlateau = 10,
    kPlateauTop = 11,
    kFractures = 16,
    kSeamounts = 17,
    kClusters = 18,
};
// The rms of one octave of the gradient noise over the octaves of the hills, measured
// (sea_hills_test): their sum divided by it has an rms of about 1.
constexpr double kHillsRms = 0.324;
// Each octave of the hills over the one before: the fine ones faint, their slopes those of the
// real abyssal hills (about 5 to 15 degrees, rarely more).
constexpr double kHillsGain = 0.35;
// The density of the seamounts in their provinces: kClusterLow + kClusterRange share^2 times
// SeaParams::seamountShare (0.64 times it on average, 1.85 at most).
constexpr double kClusterLow = 0.15;
constexpr double kClusterRange = 1.7;
// The slow noises reach about +-0.6: stretched by kShareStretch before they are clamped.
constexpr double kShareStretch = 1.6;

double unitOf(std::uint64_t h) {
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

std::uint64_t drawOf(std::uint64_t seed, std::uint64_t a, std::uint64_t b) {
    return noise::deriveSeed(noise::deriveSeed(seed, a), b);
}

double shareOf(const noise::FbmOctaves& n, double x, double z) {
    const double v = static_cast<double>(n.fbm2D(x, z)) * kShareStretch;
    return 0.5 + 0.5 * std::clamp(v, -1.0, 1.0);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// A fracture zone of the cell k of a ridge: whether it holds one, where along the axis, how deep
// its trough digs and the offset of the axis on its far side.
struct Zone {
    bool present = false;
    double at = 0.0;
    double depth = 0.0;
    double offset = 0.0;
};

Zone zoneOf(std::uint64_t seed, int ridge, const SeaParams& p, std::int64_t k) {
    const std::uint64_t base =
        drawOf(seed, kFractures, static_cast<std::uint64_t>(static_cast<std::int64_t>(ridge)));
    const std::uint64_t cell = noise::deriveSeed(base, static_cast<std::uint64_t>(k));
    Zone z;
    z.present = unitOf(noise::deriveSeed(cell, 0)) < p.fractureShare;
    z.at = (static_cast<double>(k) + 0.15 + 0.7 * unitOf(noise::deriveSeed(cell, 1))) *
           p.fractureSpacing;
    z.depth = lerp(p.fractureDepthLow, p.fractureDepthHigh, unitOf(noise::deriveSeed(cell, 2)));
    z.offset = p.transformOffset * (2.0 * unitOf(noise::deriveSeed(cell, 3)) - 1.0);
    return z;
}

// The offset of the axis just before the zone of cell k: that of the nearest zone before it.
double offsetBefore(std::uint64_t seed, int ridge, const SeaParams& p, std::int64_t k) {
    constexpr int kLookBack = 8;
    for (std::int64_t j = k - 1; j >= k - kLookBack; --j) {
        const Zone z = zoneOf(seed, ridge, p, j);
        if (z.present) {
            return z.offset;
        }
    }
    return 0.0;
}

// The profile of a seamount at the share r of its radius: flat at its summit and its foot.
double coneOf(double r) {
    return r < 1.0 ? 1.0 - r * r * (3.0 - 2.0 * r) : 0.0;
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world, const SeaParams& p) {
    Noises n;
    const WorldSize plane{kPlane, kPlane};
    for (int k = 0; k < kHillOctaves; ++k) {
        n.hills[static_cast<std::size_t>(k)] =
            noise::FbmOctaves(drawOf(seed, kHills, static_cast<std::uint64_t>(k)),
                              {.octaves = 1,
                               .frequency = dmath::powInteger(2.0, k) / p.hillsAcross,
                               .lattices = noise::OctaveLattices::Unrotated},
                              plane);
    }
    n.amplitude = noise::FbmOctaves(
        noise::deriveSeed(seed, kAmplitude),
        {.octaves = 2, .frequency = 1.0 / 600'000.0, .lattices = noise::OctaveLattices::Unrotated},
        world);
    for (std::size_t k = 0; k < n.margins.size(); ++k) {
        n.margins[k] = noise::FbmOctaves(drawOf(seed, kMargins, k),
                                         {.octaves = 2,
                                          .frequency = 1.0 / p.marginWavelength,
                                          .lattices = noise::OctaveLattices::Unrotated},
                                         world);
    }
    n.plateau = noise::FbmOctaves(noise::deriveSeed(seed, kPlateau),
                                  {.octaves = 3,
                                   .frequency = 1.0 / p.plateauWavelength,
                                   .lattices = noise::OctaveLattices::Unrotated},
                                  world);
    n.seamountClusters = noise::FbmOctaves(noise::deriveSeed(seed, kClusters),
                                           {.octaves = 2,
                                            .frequency = 1.0 / p.seamountClusterWavelength,
                                            .lattices = noise::OctaveLattices::Unrotated},
                                           world);
    n.plateauTop = noise::FbmOctaves(noise::deriveSeed(seed, kPlateauTop),
                                     {.octaves = 2,
                                      .frequency = 4.0 / p.plateauWavelength,
                                      .lattices = noise::OctaveLattices::Unrotated},
                                     world);
    return n;
}

double plateauNoise(const Noises& n, double x, double z) {
    return static_cast<double>(n.plateau.fbm2D(x, z));
}

double plateauTop(const Noises& n, const SeaParams& p, double x, double z) {
    return lerp(p.plateauTopLow, p.plateauTopHigh, shareOf(n.plateauTop, x, z));
}

double hillsAt(const Noises& n, const SeaParams& p, double u, double v, double footprint) {
    // Along the axis the hills are hillsAlong / hillsAcross times longer: u squeezed by that.
    const double x = u * (p.hillsAcross / p.hillsAlong);
    double sum = 0.0;
    double amplitude = 1.0;
    for (int k = 0; k < kHillOctaves; ++k) {
        const double wavelength = p.hillsAcross / dmath::powInteger(2.0, k);
        const double keep =
            footprint > 0.0 ? sea_floor::step(2.0 * footprint, 4.0 * footprint, wavelength) : 1.0;
        if (keep > 0.0) {
            sum += amplitude * keep *
                   static_cast<double>(n.hills[static_cast<std::size_t>(k)].fbm2D(x, v));
        }
        amplitude *= kHillsGain;
    }
    return sum / kHillsRms;
}

double hillsAmplitude(const Noises& n, const SeaParams& p, double x, double z) {
    const double v =
        std::clamp(static_cast<double>(n.amplitude.fbm2D(x, z)) * kShareStretch, -1.0, 1.0);
    return 1.0 + p.hillsVariation * v;
}

sea_floor::Margin marginOf(const Noises& n, const SeaParams& p, double x, double z, double active) {
    sea_floor::Margin m;
    const double shelf = shareOf(n.margins[0], x, z);
    const double slope = shareOf(n.margins[2], x, z);
    m.shelfWidth = lerp(lerp(p.passiveShelfLow, p.passiveShelfHigh, shelf),
                        lerp(p.activeShelfLow, p.activeShelfHigh, shelf), active);
    m.breakDepth = lerp(p.breakLow, p.breakHigh, shareOf(n.margins[1], x, z));
    m.slopeGradient = lerp(lerp(p.passiveSlopeLow, p.passiveSlopeHigh, slope),
                           lerp(p.activeSlopeLow, p.activeSlopeHigh, slope), active);
    m.footDepth = lerp(p.footLow, p.footHigh, shareOf(n.margins[3], x, z));
    // An active margin has no rise: the slope goes on down into the trench.
    m.riseGradient = lerp(p.riseLow, p.riseHigh, shareOf(n.margins[4], x, z));
    m.riseShare = 1.0 - active;
    // A terrace on the stretches of passive margin where its noise is highest, widening from 0 at
    // their edges.
    const double terrace = (shareOf(n.margins[5], x, z) - (1.0 - p.terraceShare)) / p.terraceShare;
    if (terrace > 0.0 && active < 1.0) {
        m.terraceWidth = p.terraceWidthHigh * std::min(1.0, terrace) * (1.0 - active);
        m.terraceDepth = lerp(p.terraceDepthLow, p.terraceDepthHigh, shareOf(n.margins[6], x, z));
    }
    return m;
}

sea_floor::Margin islandMarginOf(const Noises& n, const SeaParams& p, double x, double z) {
    sea_floor::Margin m;
    m.shelfWidth = lerp(p.islandShelfLow, p.islandShelfHigh, shareOf(n.margins[0], x, z));
    m.breakDepth = lerp(p.islandBreakLow, p.islandBreakHigh, shareOf(n.margins[1], x, z));
    m.slopeGradient = lerp(p.islandSlopeLow, p.islandSlopeHigh, shareOf(n.margins[2], x, z));
    m.riseShare = 0.0;
    return m;
}

Fracture fractureAt(std::uint64_t seed, int ridge, const SeaParams& p, double u) {
    const auto cell = static_cast<std::int64_t>(std::floor(u / p.fractureSpacing));
    Zone nearest;
    std::int64_t nearestCell = cell;
    double best = std::numeric_limits<double>::infinity();
    for (std::int64_t k = cell - 1; k <= cell + 1; ++k) {
        const Zone z = zoneOf(seed, ridge, p, k);
        if (z.present && std::abs(u - z.at) < best) {
            best = std::abs(u - z.at);
            nearest = z;
            nearestCell = k;
        }
    }
    Fracture f;
    if (!nearest.present) {
        f.offset = offsetBefore(seed, ridge, p, cell + 1);
        return f;
    }
    const double before = offsetBefore(seed, ridge, p, nearestCell);
    const double t =
        sea_floor::step(nearest.at - p.transformBlend, nearest.at + p.transformBlend, u);
    f.offset = before + (nearest.offset - before) * t;
    const double w = best / p.fractureWidth;
    f.trough = w < 1.0 ? nearest.depth * (1.0 - w * w) * (1.0 - w * w) : 0.0;
    return f;
}

double seamountsAt(const Noises& n, std::uint64_t seed, const WorldSize& world, const SeaParams& p,
                   double x, double z, double floor) {
    const double depth = -floor;
    const double where = sea_floor::step(1'500.0, 2'500.0, depth);
    if (!(where > 0.0)) {
        return 0.0;
    }
    const auto cellsX = static_cast<std::int64_t>(std::llround(world.x / p.seamountCell));
    const auto cellsZ = static_cast<std::int64_t>(std::llround(world.z / p.seamountCell));
    const auto ci = static_cast<std::int64_t>(std::floor((x + 0.5 * world.x) / p.seamountCell));
    const auto cj = static_cast<std::int64_t>(std::floor((z + 0.5 * world.z) / p.seamountCell));
    const double latitude = std::abs(z) / (0.5 * world.z);
    double height = 0.0;
    for (std::int64_t dj = -1; dj <= 1; ++dj) {
        for (std::int64_t di = -1; di <= 1; ++di) {
            const std::int64_t i = ((ci + di) % cellsX + cellsX) % cellsX;
            const std::int64_t j = ((cj + dj) % cellsZ + cellsZ) % cellsZ;
            const std::uint64_t cell =
                drawOf(seed, kSeamounts, static_cast<std::uint64_t>(j * cellsX + i));
            // Their provinces: dense in some, sparse in others (the noise of the clusters at the
            // middle of the cell: a seamount is there or not whatever the point).
            const double middleX = wrapCoordinate(
                -0.5 * world.x + (static_cast<double>(i) + 0.5) * p.seamountCell, world.x);
            const double middleZ = wrapCoordinate(
                -0.5 * world.z + (static_cast<double>(j) + 0.5) * p.seamountCell, world.z);
            const double cluster = shareOf(n.seamountClusters, middleX, middleZ);
            const double share =
                p.seamountShare * (kClusterLow + kClusterRange * cluster * cluster);
            if (!(unitOf(noise::deriveSeed(cell, 0)) < share)) {
                continue;
            }
            // The center, in the cell (unwrapped next to the point), and the cone.
            const double cx = -0.5 * world.x + (static_cast<double>(ci + di) + 0.15 +
                                                0.7 * unitOf(noise::deriveSeed(cell, 1))) *
                                                   p.seamountCell;
            const double cz = -0.5 * world.z + (static_cast<double>(cj + dj) + 0.15 +
                                                0.7 * unitOf(noise::deriveSeed(cell, 2))) *
                                                   p.seamountCell;
            const double draw = unitOf(noise::deriveSeed(cell, 3));
            const double h = lerp(p.seamountLow, p.seamountHigh, draw * draw);
            const double radius =
                h / lerp(p.flankLow, p.flankHigh, unitOf(noise::deriveSeed(cell, 4)));
            const double stretch = 1.0 + 0.5 * unitOf(noise::deriveSeed(cell, 5));
            const dmath::SineCosine turn = dmath::sinCosTurns(unitOf(noise::deriveSeed(cell, 6)));
            const double dx = x - cx;
            const double dz = z - cz;
            const double along = (dx * turn.cosine + dz * turn.sine) / stretch;
            const double across = -dx * turn.sine + dz * turn.cosine;
            const double r = dmath::hypot(along, across) / radius;
            double cone = h * coneOf(r);
            if (latitude > p.guyotLatitude && unitOf(noise::deriveSeed(cell, 7)) < p.guyotShare) {
                const double top =
                    lerp(p.guyotTopLow, p.guyotTopHigh, unitOf(noise::deriveSeed(cell, 8)));
                cone = std::min(cone, std::max(0.0, depth - top));
            }
            height = std::max(height, cone);
        }
    }
    // Never closer to the sea than summitDepth: the islands are for the chains of the hotspots.
    height = std::min(height, std::max(0.0, depth - p.summitDepth));
    return where * height;
}

} // namespace terrain::sea_hills
