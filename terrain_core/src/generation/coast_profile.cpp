#include "coast_profile.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace terrain::coast_profile {

namespace {

// By kind, in the order of CoastKind.
// The calanques keep a straight wall (the reference calanque, coast_reference_test).
constexpr std::array<Profile, kCoastKindCount> kProfiles{{
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30'000.0, 0.0f, 1.0, 0.0f, 0.0f, 0.0f},
    {2.0f, 14.0f, 0.5f, 6.0f, 5.0f, 40.0f, 0.0f, 6.0f, 0.0f, 12'000.0, 0.5f, 300.0, 8.0f, 4.0f,
     0.4f},
    {4.0f, 180.0f, 0.8f, 25.0f, 10.0f, 120.0f, 0.0f, 10.0f, 0.8f, 15'000.0, 0.8f, 200.0, 24.0f,
     10.0f, 0.5f},
    {8.0f, 200.0f, 0.9f, 40.0f, 0.0f, 10.0f, 0.0f, 10.0f, 0.6f, 10'000.0, 1.0f, 100.0, 0.0f,
     0.0f, 0.0f},
    {4.0f, 50.0f, 0.8f, 15.0f, 0.0f, 5.0f, 40.0f, 120.0f, 0.0f, 10'000.0, 0.5f, 300.0, 0.0f,
     0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, 1.0f, 150.0, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0, 1.0f, 120.0, 0.0f, 0.0f, 0.0f},
}};

// The height of the faces: the slow noise, contrasted from kStretchLow to kStretchHigh, times
// kMediumLow to 1 by the medium one: from a few meters to the highest along the same coast.
constexpr double kStretchLow = -0.45;
constexpr double kStretchHigh = 0.45;
constexpr double kMediumLow = 0.3;
// The screes stand where their noise passes kScreeLow to kScreeHigh (coast_shape.cpp).
constexpr double kScreeLow = 0.45;
constexpr double kScreeHigh = 0.75;
// The blocks of the walls: their sites jittered by up to kBlockJitter of a cell (the nearest site
// always among the nine around the cell); each moves the wall by its own draw, the sites within
// an edge of the nearest blended in (each its own width, from kSmallEdge or kWideEdge meters): at
// their edge, the average, a corner sharp or soft, never a seam, and none where three blocks meet.
// The small ones (coast_shape::kBlockCell) weigh kSmallBlocks all along the walls; the wide ones
// (kWideBlockCell, their corners softer: the views from afar) kWideBlocks where the noise of the
// blockiness passes kBlockyLow to kBlockyHigh, none elsewhere: stretches of massive blocks among
// stretches of small ones, their widths unlike (coast_far_test).
constexpr double kBlockJitter = 0.35;
constexpr std::array<double, 2> kSmallEdge{8.0, 14.0};
constexpr std::array<double, 2> kWideEdge{12.0, 22.0};
constexpr double kSmallBlocks = 0.8;
constexpr double kWideBlocks = 0.9;
constexpr double kBlockyLow = -0.25;
constexpr double kBlockyHigh = 0.25;
// The meanders of the walls: their noise contrasted, whole from kMeanderSpan of it.
constexpr double kMeanderSpan = 0.5;
// The dips of the top: where their noise passes kDipLow to kDipHigh.
constexpr double kDipLow = -0.35;
constexpr double kDipHigh = 0.45;

double hashUnit(std::uint64_t hash, unsigned shift) {
    return static_cast<double>((hash >> shift) & 0xFF'FFFFU) * 0x1.0p-24;
}

// The draw of the block of the walls at the canonical (x, z), in [-1, 1], their edges edge[0] to
// edge[1] meters wide.
double blockAt(const coast_shape::Blocks& b, double x, double z,
               const std::array<double, 2>& edges) {
    const double sizeX = b.world.x / static_cast<double>(b.cellsX);
    const double sizeZ = b.world.z / static_cast<double>(b.cellsZ);
    const double cx = (x + 0.5 * b.world.x) / sizeX;
    const double cz = (z + 0.5 * b.world.z) / sizeZ;
    const auto i = static_cast<std::int64_t>(std::floor(cx));
    const auto j = static_cast<std::int64_t>(std::floor(cz));
    std::array<double, 9> distance{};
    std::array<double, 9> draw{};
    std::array<double, 9> edge{};
    double nearest = std::numeric_limits<double>::infinity();
    std::size_t n = 0;
    for (std::int64_t dj = -1; dj <= 1; ++dj) {
        for (std::int64_t di = -1; di <= 1; ++di, ++n) {
            const std::int64_t ci = ((i + di) % b.cellsX + b.cellsX) % b.cellsX;
            const std::int64_t cj = ((j + dj) % b.cellsZ + b.cellsZ) % b.cellsZ;
            const std::uint64_t hash =
                noise::deriveSeed(b.seed, static_cast<std::uint64_t>(cj * b.cellsX + ci));
            const double sx = static_cast<double>(di) + 0.5 +
                              kBlockJitter * (2.0 * hashUnit(hash, 40U) - 1.0);
            const double sz = static_cast<double>(dj) + 0.5 +
                              kBlockJitter * (2.0 * hashUnit(hash, 16U) - 1.0);
            distance[n] = dmath::hypot((cx - static_cast<double>(i) - sx) * sizeX,
                                       (cz - static_cast<double>(j) - sz) * sizeZ);
            draw[n] = 2.0 * hashUnit(hash, 0U) - 1.0;
            edge[n] = edges[0] +
                      (edges[1] - edges[0]) * hashUnit(noise::deriveSeed(hash, 1U), 0U);
            nearest = std::min(nearest, distance[n]);
        }
    }
    double sum = 0.0;
    double weights = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double w = 1.0 - smooth(0.0, edge[k], distance[k] - nearest);
        sum += w * draw[k];
        weights += w;
    }
    return sum / weights;
}

} // namespace

const Profile& profileOf(CoastKind kind) {
    return kProfiles[static_cast<std::size_t>(kind)];
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double lerp(float low, float high, double t) {
    return static_cast<double>(low) + static_cast<double>(high - low) * t;
}

double unit(const noise::FbmOctaves& n, double x, double z) {
    return std::clamp(0.5 + 0.5 * static_cast<double>(n.fbm2D(x, z)), 0.0, 1.0);
}

Shore shoreAt(const coast_shape::Noises& n, double x, double z) {
    const double stretch =
        smooth(kStretchLow, kStretchHigh, static_cast<double>(n.stretch.fbm2D(x, z)));
    return {stretch * (kMediumLow + (1.0 - kMediumLow) * unit(n.height, x, z)),
            smooth(0.2, 0.8, unit(n.platform, x, z)), unit(n.face, x, z),
            2.0 * smooth(0.2, 0.8, unit(n.edge, x, z)) - 1.0,
            smooth(kScreeLow, kScreeHigh, unit(n.scree, x, z))};
}

Along alongOf(const coast_shape::Noises& n, double x, double z, double distance, double plain,
              double airX, double airZ, const coast_kinds::Weights& weights) {
    const auto weight = [&weights](CoastKind kind) {
        return static_cast<double>(weights[static_cast<std::size_t>(kind)]);
    };
    const double varied = weight(CoastKind::Rocky) + weight(CoastKind::Cliff);
    const double walled = varied + weight(CoastKind::Calanque) + weight(CoastKind::Ria) +
                          weight(CoastKind::Mountain) + weight(CoastKind::Fjord);
    const double share = walled > 0.0 ? varied / walled : 0.0;
    return {noise::wrapCanonical(x + plain * airX, n.blocks.world.x),
            noise::wrapCanonical(z + plain * airZ, n.blocks.world.z),
            smooth(kWallShareLow, kWallShareHigh, share) *
                (1.0 - smooth(kBendLow, kBendHigh, std::abs(plain - distance)))};
}

Shore shoreAt(const coast_shape::Noises& n, double x, double z, const Along& along) {
    Shore s = shoreAt(n, x, z);
    s.wall = along.wall;
    if (s.wall > 0.0) {
        s.meanderAt = 2.0 * smooth(-kMeanderSpan, kMeanderSpan,
                                   static_cast<double>(n.meander.fbm2D(along.x, along.z))) -
                      1.0;
        const double wide = smooth(kBlockyLow, kBlockyHigh,
                                   static_cast<double>(n.blockiness.fbm2D(along.x, along.z)));
        s.blockAt = kSmallBlocks * blockAt(n.blocks, along.x, along.z, kSmallEdge) +
                    wide * kWideBlocks * blockAt(n.wideBlocks, along.x, along.z, kWideEdge);
        s.dipAt = smooth(kDipLow, kDipHigh, static_cast<double>(n.dip.fbm2D(along.x, along.z)));
    }
    return s;
}

Face faceAt(const Profile& k, double top, double d, const Shore& s) {
    Face f;
    const double shift = s.wall * (static_cast<double>(k.meander) * s.meanderAt +
                                   static_cast<double>(k.blocks) * s.blockAt);
    f.foot = std::max(0.0, lerp(k.platformLow, k.platformHigh, s.platformAt) +
                               kEdgeShift * s.edgeAt + shift);
    const double rise = kBenchRise * std::min(d, f.foot) / std::max(f.foot, kBenchRun);
    f.bench = (kBenchLow + rise) * smooth(0.0, kBenchFoot, d);
    f.height = std::max(0.0, top - f.bench);
    f.width = kDistanceSlack * kSmoothSlope * f.height / coast_shape::faceSlopeLimit(f.height) +
              lerp(k.faceLow, k.faceHigh, s.faceAt);
    f.width = std::max(f.width, kBenchFoot);
    return f;
}

double faceTop(const Profile& k, const Shore& s, double land) {
    const double high = lerp(k.heightLow, k.heightHigh, s.heightAt);
    const double cap = static_cast<double>(k.landShare) * std::max(0.0, land) +
                       static_cast<double>(k.landTop);
    return std::min(high, cap);
}

Face wallFaceAt(const Profile& k, double top, double d, const Shore& s) {
    const Face whole = faceAt(k, top, d, s);
    const double dip = s.wall * static_cast<double>(k.dip) * s.dipAt;
    if (!(dip > 0.0)) {
        return whole;
    }
    const double end = whole.foot + whole.width;
    return faceAt(k, top * (1.0 - dip * (1.0 - smooth(end, end + kDipReach, d))), d, s);
}

Run runOf(const Face& f, bool plunges) {
    const double b = f.foot + f.width;
    if (plunges) {
        return {-b, 0.0, b, true};
    }
    const double m = std::max(f.foot, 0.5 * (kBenchFoot + b));
    return {2.0 * m - b, m, b, false};
}

} // namespace terrain::coast_profile
