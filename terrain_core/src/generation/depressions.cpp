#include "depressions.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::depressions {

namespace {

// Cells of about kCell meters, kShare of them with a candidate within kJitter of a cell from
// their center (so at least 180 km from its sides: no depression reaches past its cell).
constexpr double kCell = 1'200'000.0;
constexpr double kShare = 0.55;
constexpr double kJitter = 0.35;
// A candidate is a depression where the geography at its center is land at least kInland meters
// from the sea, its regional elevation at most kHighest meters, the intensity of its relief at
// most kPlain (a plain), with neither belt nor rift.
constexpr double kInland = 350'000.0;
constexpr float kHighest = 450.0f;
constexpr float kPlain = 0.07f;
// Mean radii from kRadiusLow to kRadiusHigh meters, axes up to kElongation to 1, floors from
// kFloorLow to kFloorHigh meters below the sea (most of them shallow), steep sides up to
// kEscarpment (the rim that much nearer there).
constexpr double kRadiusLow = 20'000.0;
constexpr double kRadiusHigh = 60'000.0;
constexpr double kElongation = 2.6;
constexpr double kFloorLow = 30.0;
constexpr double kFloorHigh = 420.0;
constexpr double kEscarpment = 0.35;
// The space of a basin is warped by up to kWarp meters (3 octaves from kWarpWavelength), its rim
// lobed by up to kLobes of the way (3 octaves from kLobeWavelength); its floor is flat within
// kFlat of the way to the rim, and up to kSteep more of it on its steep side (an escarpment: the
// ground drops from the rim over the last part only).
constexpr double kWarp = 9'000.0;
constexpr double kWarpWavelength = 30'000.0;
constexpr double kLobes = 0.25;
constexpr double kLobeWavelength = 14'000.0;
constexpr double kFlat = 0.4;
constexpr double kSteep = 0.45;
// The rim lies at most kReach long radii, plus the warp, from the center. The depressions that
// may reach a point are looked for on a lattice of kReachCell meters (it divides every world).
constexpr double kReach = 1.0 + kLobes + kEscarpment;
constexpr double kReachCell = 65'536.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Field k (16 bits) of a hash, in [0, 1).
double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The difference a - b brought within half a period.
double wrapped(double d, double period) {
    return d - period * std::floor(d / period + 0.5);
}

// The candidate of cell (i, j), in range, if the geography takes it.
std::optional<Site> candidate(std::uint64_t seed, const Geography& geography, int i, int j,
                              int cellsX, int cellsZ) {
    const WorldSize& world = geography.world();
    const std::uint64_t a =
        noise::deriveSeed(seed, static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(cellsX) +
                                    static_cast<std::uint64_t>(i));
    if (!(field(a, 0) < kShare)) {
        return std::nullopt;
    }
    const double sizeX = world.x / cellsX;
    const double sizeZ = world.z / cellsZ;
    Site s;
    s.x = (i + 0.5 + kJitter * (2.0 * field(a, 1) - 1.0)) * sizeX - 0.5 * world.x;
    s.z = (j + 0.5 + kJitter * (2.0 * field(a, 2) - 1.0)) * sizeZ - 0.5 * world.z;
    const GeographySample g = geography.sampleAt(s.x, s.z);
    if (!(g.coast >= kInland) || !(g.land <= kHighest) || !(g.relief <= kPlain) ||
        g.belt > 0.0f || g.rift > 0.0f) {
        return std::nullopt;
    }
    const std::uint64_t b = noise::deriveSeed(a, 1);
    s.radius = kRadiusLow + (kRadiusHigh - kRadiusLow) * field(a, 3);
    s.elongation = 1.0 + (kElongation - 1.0) * field(b, 0);
    s.angle = field(b, 1);
    const double u = field(b, 2);
    s.floor = -(kFloorLow + (kFloorHigh - kFloorLow) * u * u);
    s.escarpment = kEscarpment * field(b, 3);
    s.steepSide = field(noise::deriveSeed(a, 2), 0);
    return s;
}

// The share of the way to its floor at (x, z), (dx, dz) from its center: 1 on its flat floor, 0
// beyond its rim.
double shareOf(const Sites& sites, const Site& s, double x, double z, double dx, double dz) {
    const std::array<float, 2> warp = sites.warpX.fbm2DPair(sites.warpZ, x, z);
    dx += kWarp * static_cast<double>(warp[0]);
    dz += kWarp * static_cast<double>(warp[1]);
    const double c = dmath::cosTurns(s.angle);
    const double n = dmath::sinTurns(s.angle);
    const double along = dx * c + dz * n;
    const double across = dz * c - dx * n;
    const double root = std::sqrt(s.elongation);
    const double ellipse = dmath::hypot(along / (s.radius * root), across * root / s.radius);
    const double toward = dmath::cosTurns(dmath::atan2(dz, dx) / kTwoPi - s.steepSide);
    const double rim = 1.0 + kLobes * static_cast<double>(sites.outline.fbm2D(x, z)) -
                       s.escarpment * toward;
    const double side = std::max(0.0, toward);
    return 1.0 - smooth(kFlat + kSteep * side * side, 1.0, ellipse / rim);
}

// The lattice cell holding the canonical coordinate v of a side of the world, wrapped.
int reachCell(double v, double period, int count) {
    const int c = static_cast<int>(std::floor((v + 0.5 * period) / kReachCell));
    return (c % count + count) % count;
}

// How far the rim of s may lie from its center, meters.
double reachOf(const Site& s) {
    return kReach * s.radius * std::sqrt(s.elongation) + kWarp;
}

// Marks the cells of the lattice that site k may reach: those within its reach of its center.
void markReach(Sites& sites, std::size_t k) {
    const Site& s = sites.sites[k];
    const double r = reachOf(s);
    const int span = static_cast<int>(std::ceil(r / kReachCell)) + 1;
    const int i0 = reachCell(s.x, sites.world.x, sites.reachX);
    const int j0 = reachCell(s.z, sites.world.z, sites.reachZ);
    // The center within its cell, meters from the corner of the cell.
    const double ox = s.x + 0.5 * sites.world.x - i0 * kReachCell;
    const double oz = s.z + 0.5 * sites.world.z - j0 * kReachCell;
    for (int dj = -span; dj <= span; ++dj) {
        for (int di = -span; di <= span; ++di) {
            const double nx = std::clamp(ox, di * kReachCell, (di + 1) * kReachCell) - ox;
            const double nz = std::clamp(oz, dj * kReachCell, (dj + 1) * kReachCell) - oz;
            if (!(nx * nx + nz * nz < r * r)) {
                continue;
            }
            const int i = ((i0 + di) % sites.reachX + sites.reachX) % sites.reachX;
            const int j = ((j0 + dj) % sites.reachZ + sites.reachZ) % sites.reachZ;
            std::array<std::uint16_t, 3>& slots =
                sites.reach[static_cast<std::size_t>(j) * static_cast<std::size_t>(sites.reachX) +
                            static_cast<std::size_t>(i)];
            *std::find(slots.begin(), slots.end() - 1, std::uint16_t{0}) =
                static_cast<std::uint16_t>(k + 1);
        }
    }
}

// The slots of the cell of the lattice holding the canonical (x, z).
const std::array<std::uint16_t, 3>& reachAt(const Sites& sites, double x, double z) {
    const int i = reachCell(x, sites.world.x, sites.reachX);
    const int j = reachCell(z, sites.world.z, sites.reachZ);
    return sites.reach[static_cast<std::size_t>(j) * static_cast<std::size_t>(sites.reachX) +
                       static_cast<std::size_t>(i)];
}

} // namespace

Sites makeSites(std::uint64_t seed, const Geography& geography) {
    const WorldSize& world = geography.world();
    Sites sites;
    sites.world = world;
    const int cellsX = std::max(1, static_cast<int>(std::lround(world.x / kCell)));
    const int cellsZ = std::max(1, static_cast<int>(std::lround(world.z / kCell)));
    const auto octaves = [&](std::uint64_t stream, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = 3,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    sites.outline = octaves(1, kLobeWavelength);
    sites.warpX = octaves(2, kWarpWavelength);
    sites.warpZ = octaves(3, kWarpWavelength);
    const std::uint64_t cells = noise::deriveSeed(seed, 0);
    for (int j = 0; j < cellsZ; ++j) {
        for (int i = 0; i < cellsX; ++i) {
            if (const std::optional<Site> s = candidate(cells, geography, i, j, cellsX, cellsZ)) {
                sites.sites.push_back(*s);
            }
        }
    }
    sites.reachX = static_cast<int>(std::lround(world.x / kReachCell));
    sites.reachZ = static_cast<int>(std::lround(world.z / kReachCell));
    sites.reach.assign(static_cast<std::size_t>(sites.reachX) *
                           static_cast<std::size_t>(sites.reachZ),
                       {0, 0, 0});
    for (std::size_t k = 0; k < sites.sites.size(); ++k) {
        markReach(sites, k);
    }
    return sites;
}

bool mayReach(const Sites& sites, double x, double z) {
    return reachAt(sites, x, z)[0] != 0;
}

Basin basinAt(const Sites& sites, double x, double z) {
    Basin best;
    for (const std::uint16_t k : reachAt(sites, x, z)) {
        if (k == 0) {
            break;
        }
        const Site& s = sites.sites[k - 1U];
        const double dx = wrapped(x - s.x, sites.world.x);
        const double dz = wrapped(z - s.z, sites.world.z);
        const double reach = reachOf(s);
        if (!(dx * dx + dz * dz < reach * reach)) {
            continue;
        }
        const double share = shareOf(sites, s, x, z, dx, dz);
        if (share > best.share) {
            best = {share, s.floor};
        }
    }
    return best;
}

} // namespace terrain::depressions
