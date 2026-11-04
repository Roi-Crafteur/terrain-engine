// The grabens of the rifts (rift_grabens.hpp).
#include "rift_grabens.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace terrain::lows {

namespace {

// A node of the map is a candidate on the axis of a rift (kRift at least kAxisNode) with the
// share kShare (times the density), its center confirmed at kAxisCenter; kept kSpacing meters
// from the others at least.
constexpr float kAxisNode = 0.9f;
constexpr float kAxisCenter = 0.85f;
constexpr double kShare = 0.08;
constexpr double kSpacing = 600'000.0;
// Half lengths, half widths and floors; the floor of the rift there at most kHighestFloor; the
// footprint without relief (at most kPlain), kInland meters from the sea at least, in the trough
// (kRift at least kInTrough at the ends of its axis and kAcross at its sides).
constexpr double kLengthLow = 40'000.0;
constexpr double kLengthHigh = 125'000.0;
constexpr double kWidthLow = 7'500.0;
constexpr double kWidthHigh = 20'000.0;
constexpr double kFloorLow = 100.0;
constexpr double kFloorHigh = 450.0;
constexpr float kHighestFloor = 500.0f;
constexpr float kPlain = 0.08f;
constexpr double kInland = 40'000.0;
constexpr float kInTrough = 0.5f;
constexpr float kAcross = 0.7f;
// The axis: the best of kDirections directions, the rift read kProbe and twice it along each.
constexpr int kDirections = 16;
constexpr double kProbe = 20'000.0;
// The profile: the floor over kFlat of the half width, the ends tapering over kTaper of the half
// length, the axis bent by up to kBend half widths and the width varied by kWidthVary (noises of
// kWarpWavelength), the faults moved by up to kStepJitter of the walls; benches tilted by kTilt of
// a step; faces kFaceRun times as wide as high.
constexpr double kFlat = 0.45;
constexpr double kTaper = 0.35;
constexpr double kBend = 0.25;
constexpr double kWidthVary = 0.2;
constexpr double kStepJitter = 0.08;
constexpr double kTilt = 0.15;
constexpr double kFaceRun = 0.5;
constexpr double kWarpWavelength = 30'000.0;
constexpr double kStepWavelength = 6'000.0;

double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double wrapped(double d, double period) {
    return d - period * std::floor(d / period + 0.5);
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The canonical point of the world under node (i, j) of the map.
std::array<double, 2> nodePoint(const Geography& g, int i, int j) {
    const WorldSize& w = g.world();
    const double geoX = i * (w.x / g.columns()) - 0.5 * w.x;
    const double z = j * (w.z / g.rows()) - 0.5 * w.z;
    return {noise::wrapCanonical(geoX - g.anchorX(), w.x), noise::wrapCanonical(z, w.z)};
}

// The direction (turns) along which the rift stays deepest through (x, z).
double axisTurns(const Geography& g, double x, double z) {
    double best = 0.0;
    double bestScore = -1.0;
    for (int k = 0; k < kDirections; ++k) {
        const double turns = 0.5 * k / kDirections;
        const double ux = dmath::cosTurns(turns);
        const double uz = dmath::sinTurns(turns);
        double score = 0.0;
        for (const double d : {-2.0 * kProbe, -kProbe, kProbe, 2.0 * kProbe}) {
            score += static_cast<double>(g.sampleAt(x + d * ux, z + d * uz).rift);
        }
        if (score > bestScore) {
            bestScore = score;
            best = turns;
        }
    }
    return best;
}

// Whether the footprint of s lies on land without relief, far from the sea, in the trough.
bool accepted(const Geography& g, const Graben& s) {
    const double px = -s.axisZ;
    const double pz = s.axisX;
    const std::array<std::array<double, 3>, 9> probes = {{
        {0.0, 0.0, 0.0},
        {s.length, 0.0, kInTrough},
        {-s.length, 0.0, kInTrough},
        {0.0, s.width, kAcross},
        {0.0, -s.width, kAcross},
        {0.7 * s.length, 0.7 * s.width, 0.0},
        {0.7 * s.length, -0.7 * s.width, 0.0},
        {-0.7 * s.length, 0.7 * s.width, 0.0},
        {-0.7 * s.length, -0.7 * s.width, 0.0},
    }};
    for (const auto& [a, b, rift] : probes) {
        const GeographySample p =
            g.sampleAt(s.x + a * s.axisX + b * px, s.z + a * s.axisZ + b * pz);
        if (!(p.coast >= kInland) || !(p.relief <= kPlain) || p.rift < static_cast<float>(rift)) {
            return false;
        }
    }
    return true;
}

// The graben a candidate node makes, if the geography takes it.
std::optional<Graben> candidate(std::uint64_t hash, const Geography& g, double x, double z) {
    const GeographySample c = g.sampleAt(x, z);
    if (c.rift < kAxisCenter || !(c.land <= kHighestFloor)) {
        return std::nullopt;
    }
    const std::uint64_t b = noise::deriveSeed(hash, 1);
    Graben s;
    s.x = x;
    s.z = z;
    const double turns = axisTurns(g, x, z);
    s.axisX = dmath::cosTurns(turns);
    s.axisZ = dmath::sinTurns(turns);
    s.length = kLengthLow + (kLengthHigh - kLengthLow) * field(hash, 1);
    s.width = kWidthLow + (kWidthHigh - kWidthLow) * field(hash, 2);
    const double u = field(hash, 3);
    s.floor = -(kFloorLow + (kFloorHigh - kFloorLow) * u * u);
    s.steps = 2 + static_cast<int>(field(b, 0) * 3.0);
    const double drop = static_cast<double>(c.land) - s.floor;
    const double stepWidth = (1.0 - kFlat) * s.width / s.steps;
    s.faceShare = std::clamp(kFaceRun * drop / s.steps / stepWidth, 0.02, 0.5);
    s.reach = (1.0 + kWidthVary) * dmath::hypot(s.length, s.width) + kBend * s.width;
    if (!accepted(g, s)) {
        return std::nullopt;
    }
    return s;
}

bool farFromAll(const std::vector<Graben>& sites, const WorldSize& w, double x, double z) {
    return std::all_of(sites.begin(), sites.end(), [&](const Graben& s) {
        const double dx = wrapped(x - s.x, w.x);
        const double dz = wrapped(z - s.z, w.z);
        return dx * dx + dz * dz >= kSpacing * kSpacing;
    });
}

// The share of the way down to the floor of s at (x, z), (dx, dz) from its center.
double shareOf(const Grabens& grabens, const Graben& s, double x, double z, double dx,
               double dz) {
    const double along = std::abs(dx * s.axisX + dz * s.axisZ) / s.length;
    if (!(along < 1.0)) {
        return 0.0;
    }
    const double across = std::abs(-dx * s.axisZ + dz * s.axisX +
                                   kBend * s.width * grabens.warp.fbm2D(x, z));
    const double width = s.width * (1.0 + kWidthVary * grabens.widths.fbm2D(x, z)) *
                         std::sqrt(std::min(1.0, (1.0 - along) / kTaper));
    const double t = across / width;
    if (!(t < 1.0)) {
        return 0.0;
    }
    if (t <= kFlat) {
        return 1.0;
    }
    double wall = (t - kFlat) / (1.0 - kFlat);
    // The faults wander, never at the floor nor at the rim.
    wall += 4.0 * wall * (1.0 - wall) * kStepJitter * grabens.steps.fbm2D(x, z);
    wall = std::clamp(wall, 0.0, 1.0) * s.steps;
    const double k = std::min(std::floor(wall), static_cast<double>(s.steps - 1));
    const double f = wall - k;
    const double face = smooth(0.0, s.faceShare, f);
    return 1.0 - (k + (1.0 - kTilt) * face + kTilt * f) / s.steps;
}

} // namespace

Grabens makeGrabens(std::uint64_t seed, const Geography& geography, double density) {
    const WorldSize& world = geography.world();
    const auto octaves = [&](std::uint64_t stream, int count, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = count,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    Grabens out;
    out.warp = octaves(1, 3, kWarpWavelength);
    out.widths = octaves(2, 2, kWarpWavelength);
    out.steps = octaves(3, 2, kStepWavelength);
    const std::uint64_t nodes = noise::deriveSeed(seed, 4);
    for (int j = 0; j < geography.rows(); ++j) {
        for (int i = 0; i < geography.columns(); ++i) {
            if (geography.node(i, j, Geography::kRift) < kAxisNode) {
                continue;
            }
            const std::uint64_t hash = noise::deriveSeed(
                nodes, static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(geography.columns()) +
                           static_cast<std::uint64_t>(i));
            if (!(field(hash, 0) < kShare * density)) {
                continue;
            }
            const auto [x, z] = nodePoint(geography, i, j);
            if (!farFromAll(out.sites, world, x, z)) {
                continue;
            }
            if (const std::optional<Graben> s = candidate(hash, geography, x, z)) {
                out.sites.push_back(*s);
            }
        }
    }
    out.reach = ReachLattice(world, 65'536.0);
    for (std::size_t k = 0; k < out.sites.size(); ++k) {
        out.reach.add(static_cast<std::uint32_t>(k), out.sites[k].x, out.sites[k].z,
                      out.sites[k].reach);
    }
    out.reach.finish();
    return out;
}

GrabenPlace grabenAt(const Grabens& grabens, const WorldSize& world, double x, double z) {
    GrabenPlace best;
    for (const std::uint32_t k : grabens.reach.at(x, z)) {
        const Graben& s = grabens.sites[k];
        const double dx = wrapped(x - s.x, world.x);
        const double dz = wrapped(z - s.z, world.z);
        if (!(dx * dx + dz * dz < s.reach * s.reach)) {
            continue;
        }
        const double share = shareOf(grabens, s, x, z, dx, dz);
        if (share > best.share) {
            best = {share, s.floor};
        }
    }
    return best;
}

} // namespace terrain::lows
