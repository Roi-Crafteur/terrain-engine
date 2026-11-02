#include "coast_reefs.hpp"

#include "coast_shape.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace terrain::coast_reefs {

namespace {

using coast_inlets::kSignalPerMeter;
using coast_inlets::Stack;

constexpr double kPi = 3.14159265358979323846;
// Two to four rocks within kReefSpread meters of the middle of the reef, kReefGap meters of sea
// at least between them and the shore of the geography; the first rock kFirstRock meters high,
// the others kOtherRock; each rock kRockGap meters of sea at least from that shore.
constexpr std::array<double, 2> kReefSpread{25.0, 70.0};
constexpr double kReefGap = 120.0;
constexpr std::array<double, 2> kFirstRock{6.0, 12.0};
constexpr std::array<double, 2> kOtherRock{3.0, 9.0};
constexpr double kRockGap = 30.0;
// The flank of a rock kRockEase of the slope law steep (room for the detail of the land on it).
constexpr double kRockEase = 0.8;
// The shore of a rock meets the coast signal over kIsletReach meters; overlapping rocks merge over
// kRockBlend meters of height.
constexpr double kIsletReach = 60.0;
constexpr double kRockBlend = 6.0;
// The small relief of the rocks (makeRough): 10 m and finer, 25 % of their height (35 % draws
// teeth at the LOD 1).
constexpr std::uint64_t kRoughStream = 0x2EEF;
constexpr double kRoughWavelength = 10.0;
constexpr double kRough = 0.25;

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

// A rock at u, 0 at its foot to 1 at its middle: its flank straight (steepest), its foot rounded
// over kRockRim of the way, its top over kRockTop (a dome), the slope continuous.
double domeOf(double u) {
    const double k = 1.0 / (1.0 - 0.5 * (kRockRim + kRockTop));
    if (!(u > 0.0)) {
        return 0.0;
    }
    if (u < kRockRim) {
        return 0.5 * k * u * u / kRockRim;
    }
    if (u > 1.0 - kRockTop) {
        return u < 1.0 ? 1.0 - 0.5 * k * (1.0 - u) * (1.0 - u) / kRockTop : 1.0;
    }
    return k * (u - 0.5 * kRockRim);
}

// How far the foot of a rock reaches toward turns, over its radius: 1 to 1 + kRockLobe, two and
// three lobes turned by its phase.
double lobesOf(const Stack& rock, double turns) {
    const auto phase = static_cast<double>(rock.phase);
    const double wave = 0.5 + 0.3 * dmath::sinTurns(2.0 * turns + phase) +
                        0.2 * dmath::sinTurns(3.0 * turns + 2.7 * phase + 0.21);
    return 1.0 + kRockLobe * wave;
}

} // namespace

double rockRadius(double height) {
    const double slope = kRockEase * coast_shape::faceSlopeLimit(height);
    return height / (slope * (1.0 - 0.5 * (kRockRim + kRockTop)));
}

std::vector<Stack> reefOf(const coast_inlets::World& w, coast_inlets::Draws& d, double x,
                          double z, const std::function<bool(double x, double z)>& inside) {
    std::vector<Stack> out;
    const double spread = between(kReefSpread[0], kReefSpread[1], d.next());
    const int rocks = 2 + static_cast<int>(3.0 * d.next());
    const double widest = rockRadius(kFirstRock[1]) * (1.0 + kRockLobe);
    if (!coast_inlets::rootOf(w, -kSignalPerMeter * (spread + widest + kReefGap), x, z) ||
        !inside(x, z)) {
        return out;
    }
    for (int k = 0; k < rocks; ++k) {
        const std::array<double, 2>& range = k == 0 ? kFirstRock : kOtherRock;
        const double height = between(range[0], range[1], d.next());
        const dmath::SineCosine toward = dmath::sinCosTurns(d.next());
        const double away = k == 0 ? 0.0 : spread * std::sqrt(d.next());
        const double rx = x + away * toward.cosine;
        const double rz = z + away * toward.sine;
        const double radius = rockRadius(height);
        if (w.coast(rx, rz) > -kSignalPerMeter * (radius * (1.0 + kRockLobe) + kRockGap)) {
            continue; // out at sea only, never on another shore
        }
        const double cx = wrapCoordinate(rx, w.size.x);
        const double cz = wrapCoordinate(rz, w.size.z);
        // The phase of its lobes from where it stands (no draw taken).
        const std::uint64_t h = noise::deriveSeed(std::bit_cast<std::uint64_t>(cx),
                                                  std::bit_cast<std::uint64_t>(cz));
        out.push_back({cx, cz, static_cast<float>(radius), static_cast<float>(height),
                       static_cast<float>(static_cast<double>(h >> 11) * 0x1.0p-53)});
    }
    return out;
}

noise::FbmOctaves makeRough(std::uint64_t seed, const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, kRoughStream),
                             {.octaves = 4, .frequency = 1.0 / kRoughWavelength, .gain = 0.6},
                             world);
}

void addRock(const Stack& rock, double x, double z, const WorldSize& world,
             const noise::FbmOctaves& rough, coast_inlets::Effect& e) {
    const double dx = noise::wrapCanonical(x - rock.x, world.x);
    const double dz = noise::wrapCanonical(z - rock.z, world.z);
    const double most = static_cast<double>(rock.radius) * (1.0 + kRockLobe);
    if (std::abs(dx) > most + kIsletReach || std::abs(dz) > most + kIsletReach) {
        return;
    }
    const double r = dmath::hypot(dx, dz);
    const double turns = r > 0.0 ? dmath::atan2(dz, dx) / (2.0 * kPi) : 0.0;
    const double radius = static_cast<double>(rock.radius) * lobesOf(rock, turns);
    e.islet = std::max(e.islet, r < radius ? radius - r
                                           : -coast_inlets::reachOf(r - radius, 0.0, kIsletReach));
    // Overlapping rocks merge rounded (no crease between them), never above the water at their
    // foot: the rounding vanishes there.
    double dome = static_cast<double>(rock.height) * domeOf(1.0 - r / radius);
    if (dome > 0.0) {
        dome *= 1.0 + kRough * static_cast<double>(rough.fbm2D(x, z));
    }
    const double k = std::min({kRockBlend, e.dome, dome});
    if (k > 0.0) {
        const double h = std::max(k - std::abs(e.dome - dome), 0.0) / k;
        e.dome = std::max(e.dome, dome) + 0.25 * k * h * h;
    } else {
        e.dome = std::max(e.dome, dome);
    }
}

} // namespace terrain::coast_reefs
