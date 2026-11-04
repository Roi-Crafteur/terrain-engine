// The dry basins below the sea level of agent 52 (low_basins.hpp).
#include "low_basins.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace terrain::lows {

namespace {

// Cells of about kCell meters, kShare of them (times the density) with a candidate within kJitter
// of a cell from their center.
constexpr double kCell = 500'000.0;
constexpr double kShare = 0.6;
constexpr double kJitter = 0.35;
// The geography at the center: a low plain (regional elevation at most kHighest), no belt, no
// rift; over the footprint: no relief (kPlain at most), kInland meters from the sea at least.
constexpr float kHighest = 400.0f;
constexpr float kPlain = 0.08f;
constexpr double kInland = 40'000.0;
// Mean radii, elongations, floors (most of them shallow) and escarpments.
constexpr double kRadiusLow = 14'000.0;
constexpr double kRadiusHigh = 35'000.0;
constexpr double kElongation = 2.2;
constexpr double kFloorLow = 20.0;
constexpr double kFloorHigh = 160.0;
constexpr double kEscarpment = 0.35;
// The reach of the forms of the depressions (depressions.cpp): 1.25 long radii, the escarpment,
// and their warp.
constexpr double kReach = 1.25 + kEscarpment;
constexpr double kWarp = 9'000.0;
// The footprint is probed at its center and kProbes points around at its reach.
constexpr int kProbes = 8;
// The depressions of phase 11B reach at most this many long radii (depressions.cpp).
constexpr double kDepressionReach = 1.6;

double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double wrapped(double d, double period) {
    return d - period * std::floor(d / period + 0.5);
}

bool footprintTaken(const Geography& g, const depressions::Site& s, double reach) {
    for (int k = 0; k <= kProbes; ++k) {
        const double turns = static_cast<double>(k) / kProbes;
        const double r = k == kProbes ? 0.0 : reach;
        const GeographySample p =
            g.sampleAt(s.x + r * dmath::cosTurns(turns), s.z + r * dmath::sinTurns(turns));
        if (!(p.coast >= kInland) || !(p.relief <= kPlain) || p.belt > 0.0f || p.rift > 0.0f) {
            return false;
        }
    }
    return true;
}

bool clearOf(const std::vector<depressions::Site>& others, const WorldSize& w,
             const depressions::Site& s, double reach) {
    return std::all_of(others.begin(), others.end(), [&](const depressions::Site& o) {
        const double dx = wrapped(s.x - o.x, w.x);
        const double dz = wrapped(s.z - o.z, w.z);
        const double apart = reach + kDepressionReach * o.radius * std::sqrt(o.elongation) + kWarp;
        return dx * dx + dz * dz >= apart * apart;
    });
}

std::optional<depressions::Site> candidate(std::uint64_t seed, const Geography& g, int i, int j,
                                           int cellsX, int cellsZ, double density) {
    const WorldSize& world = g.world();
    const std::uint64_t a =
        noise::deriveSeed(seed, static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(cellsX) +
                                    static_cast<std::uint64_t>(i));
    if (!(field(a, 0) < kShare * density)) {
        return std::nullopt;
    }
    depressions::Site s;
    s.x = (i + 0.5 + kJitter * (2.0 * field(a, 1) - 1.0)) * (world.x / cellsX) - 0.5 * world.x;
    s.z = (j + 0.5 + kJitter * (2.0 * field(a, 2) - 1.0)) * (world.z / cellsZ) - 0.5 * world.z;
    if (!(g.sampleAt(s.x, s.z).land <= kHighest)) {
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

} // namespace

double basinReach(const depressions::Site& s) {
    return kReach * s.radius * std::sqrt(s.elongation) + kWarp;
}

Basins makeBasins(std::uint64_t seed, const Geography& geography, double density,
                  const std::vector<depressions::Site>& depressions) {
    const WorldSize& world = geography.world();
    Basins out;
    out.forms = depressions::makeForms(noise::deriveSeed(seed, 1), world);
    const int cellsX = std::max(1, static_cast<int>(std::lround(world.x / kCell)));
    const int cellsZ = std::max(1, static_cast<int>(std::lround(world.z / kCell)));
    const std::uint64_t cells = noise::deriveSeed(seed, 0);
    for (int j = 0; j < cellsZ; ++j) {
        for (int i = 0; i < cellsX; ++i) {
            const std::optional<depressions::Site> s =
                candidate(cells, geography, i, j, cellsX, cellsZ, density);
            if (s && clearOf(depressions, world, *s, basinReach(*s)) &&
                footprintTaken(geography, *s, basinReach(*s))) {
                out.sites.push_back(*s);
            }
        }
    }
    out.reach = ReachLattice(world, 65'536.0);
    for (std::size_t k = 0; k < out.sites.size(); ++k) {
        out.reach.add(static_cast<std::uint32_t>(k), out.sites[k].x, out.sites[k].z,
                      basinReach(out.sites[k]));
    }
    out.reach.finish();
    return out;
}

depressions::Basin basinAt(const Basins& basins, const WorldSize& world, double x, double z,
                           double base) {
    depressions::Basin best;
    for (const std::uint32_t k : basins.reach.at(x, z)) {
        const depressions::Site& s = basins.sites[k];
        const double dx = wrapped(x - s.x, world.x);
        const double dz = wrapped(z - s.z, world.z);
        const double reach = basinReach(s);
        if (!(dx * dx + dz * dz < reach * reach)) {
            continue;
        }
        const depressions::Profile p = depressions::profileOf(
            basins.forms, s, x, z, dx, dz, std::max(1.0, base - s.floor));
        if (p.share > best.profile.share) {
            best = {p, s.floor};
        }
    }
    return best;
}

} // namespace terrain::lows
