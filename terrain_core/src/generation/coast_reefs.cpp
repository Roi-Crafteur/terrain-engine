#include "coast_reefs.hpp"

#include "coast_shape.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
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
// The reaches of two rocks of a reef kRockApart meters apart at least, a rock moved out by
// kRockStep meters up to kRockMove to stand so.
constexpr double kRockApart = 2.0;
constexpr double kRockStep = 4.0;
constexpr double kRockMove = 40.0;
// The flank of a rock kRockEase of the slope law steep (room for the detail of the land on it).
constexpr double kRockEase = 0.8;
// The shore of a rock meets the coast signal over kIsletReach meters; overlapping rocks merge over
// kRockBlend meters of height.
constexpr double kIsletReach = 60.0;
constexpr double kRockBlend = 6.0;
// The small relief of the rocks (makeRough): 10 m and finer, 25 % of their height (35 % draws
// teeth at the LOD 1).
constexpr std::uint64_t kRoughStream = 0x2EEF;
// One reef in about 1 / kStackShare stands a stack, kStackHeight high and kStackRadius wide at the
// water: its face in three dimensions drawn fully at the LOD 0 to 2 of the tiles (its run, 2 (1 -
// kRockTop) of its radius, 35 m and more: kRunCells cells of the LOD 2, and 1.5 times that), in
// part at the LOD 3 and on the horizon (as their cells draw it); in two dimensions its low rock
// platform. Its rough relief kRoughMost meters at most (that of the highest rocks of the reefs).
constexpr std::uint64_t kStackStream = 0x57AC;
constexpr std::uint64_t kSpireStream = 0x5F12EULL;
// Where a stack has no apron, its rock reaches kLeastApron meters beyond its foot (the islet).
constexpr double kLeastApron = 0.5;
constexpr double kStackShare = 0.1;
constexpr double kRoughMost = 12.0;
constexpr std::array<double, 2> kStackHeight{20.0, 60.0};
constexpr std::array<double, 2> kStackRadius{25.0, 35.0};
constexpr double kRoughWavelength = 10.0;
constexpr double kRough = 0.25;

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

// Whether the reef at (x, z) stands a stack, how high and how wide (hashes of its middle).
struct Tall {
    bool stack = false;
    double height = 0.0;
    double radius = 0.0;
};

Tall tallOf(double x, double z, const WorldSize& world) {
    const std::uint64_t h = noise::deriveSeed(
        kStackStream ^ std::bit_cast<std::uint64_t>(wrapCoordinate(x, world.x)),
        std::bit_cast<std::uint64_t>(wrapCoordinate(z, world.z)));
    const auto draw = [h](unsigned k) {
        return static_cast<double>((h >> (21U * k)) & 0x1F'FFFFU) * 0x1.0p-21;
    };
    if (!(draw(0) < kStackShare)) {
        return {};
    }
    return {true, between(kStackHeight[0], kStackHeight[1], draw(1)),
            between(kStackRadius[0], kStackRadius[1], draw(2))};
}

// A rock of a reef at (rock.x, rock.z): out at sea only, never on another shore; moved on toward
// (from the middle of its reef) kRockStep meters at a time, kRockMove meters at most, until its
// reach stands kRockApart meters off those of the rocks before it (a point holds one spire).
void placeRock(const coast_inlets::World& w, Stack rock, dmath::SineCosine toward,
               std::vector<Stack>& out, std::vector<double>& reaches) {
    const double radius = static_cast<double>(rock.radius);
    for (double move = 0.0; move <= kRockMove; move += kRockStep) {
        const double rx = rock.x + move * toward.cosine;
        const double rz = rock.z + move * toward.sine;
        if (w.coast(rx, rz) > -kSignalPerMeter * (radius * (1.0 + kRockLobe) + kRockGap)) {
            return;
        }
        const double cx = wrapCoordinate(rx, w.size.x);
        const double cz = wrapCoordinate(rz, w.size.z);
        // The phase of its lobes from where it stands (no draw taken).
        const std::uint64_t h = noise::deriveSeed(std::bit_cast<std::uint64_t>(cx),
                                                  std::bit_cast<std::uint64_t>(cz));
        const Stack placed{cx, cz, rock.radius, rock.height,
                           static_cast<float>(static_cast<double>(h >> 11) * 0x1.0p-53)};
        const double reach = spireOf(placed).reach;
        bool apart = true;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double gap = dmath::hypot(noise::wrapCanonical(cx - out[i].x, w.size.x),
                                            noise::wrapCanonical(cz - out[i].z, w.size.z));
            apart = apart && gap > reach + reaches[i] + kRockApart;
        }
        if (apart) {
            out.push_back(placed);
            reaches.push_back(reach);
            return;
        }
    }
}

} // namespace

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
    // About one reef in kStackShare stands a stack, its first rock (by its middle: no draw taken,
    // the other reefs keep theirs).
    const Tall tall = tallOf(x, z, w.size);
    std::vector<double> reaches;
    for (int k = 0; k < rocks; ++k) {
        const std::array<double, 2>& range = k == 0 ? kFirstRock : kOtherRock;
        double height = between(range[0], range[1], d.next());
        const dmath::SineCosine toward = dmath::sinCosTurns(d.next());
        const double away = k == 0 ? 0.0 : spread * std::sqrt(d.next());
        // A reef that stands a stack: the stack alone, the blocks at its foot for its other rocks
        // (rock_spires.hpp; their draws taken all the same).
        if (k > 0 && tall.stack) {
            continue;
        }
        double radius = rockRadius(height);
        if (k == 0 && tall.stack) {
            height = tall.height;
            radius = tall.radius;
        }
        placeRock(w, {x + away * toward.cosine, z + away * toward.sine, static_cast<float>(radius),
                      static_cast<float>(height), 0.0f},
                  toward, out, reaches);
    }
    return out;
}

noise::FbmOctaves makeRough(std::uint64_t seed, const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, kRoughStream),
                             {.octaves = 4, .frequency = 1.0 / kRoughWavelength, .gain = 0.6},
                             world);
}

std::uint32_t spireSeed(double x, double z) {
    return static_cast<std::uint32_t>(
        noise::deriveSeed(kSpireStream ^ std::bit_cast<std::uint64_t>(x),
                          std::bit_cast<std::uint64_t>(z)) >>
        32U);
}

rock_spires::Shape spireOf(const Stack& rock) {
    return rock_spires::shapeOf(spireSeed(rock.x, rock.z), static_cast<double>(rock.radius),
                                static_cast<double>(rock.height));
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
    // Its outline at the sea level, its platform beyond; its spire stands on it.
    const rock_spires::Shape shape = spireOf(rock);
    const double inner = rock_spires::footprintAt(shape, turns);
    const double radius = inner + std::max(kLeastApron, rock_spires::apronWidth(shape, turns));
    const double islet =
        r < radius ? radius - r : -coast_inlets::reachOf(r - radius, 0.0, kIsletReach);
    // Overlapping rocks merge rounded (no crease between them), never above the water at their
    // foot: the rounding vanishes there.
    double dome = rock_spires::apronAt(shape, turns) * domeOf((radius - r) / (radius - inner));
    const double amplitude =
        kRough * std::min(1.0, kRoughMost / static_cast<double>(rock.height));
    const double roughness =
        dome > 0.0 ? 1.0 + amplitude * static_cast<double>(rough.fbm2D(x, z)) : 1.0;
    dome *= roughness;
    if (shape.reach - r > e.spireIn) {
        e.spireX = rock.x;
        e.spireZ = rock.z;
        e.spireSize = static_cast<double>(rock.radius);
        e.spireHeight = static_cast<double>(rock.height);
        e.spireIn = shape.reach - r;
    }
    e.nextIslet = std::max(e.nextIslet, std::min(e.islet, islet));
    if (islet > e.islet) {
        e.rockX = rock.x;
        e.rockZ = rock.z;
        e.rockRadius = radius;
        e.rockSize = static_cast<double>(rock.radius);
        e.rockHeight = static_cast<double>(rock.height);
        e.rockTop = static_cast<double>(rock.height) * roughness;
        e.rockPhase = static_cast<double>(rock.phase);
    }
    e.islet = std::max(e.islet, islet);
    const double k = std::min({kRockBlend, e.dome, dome});
    if (k > 0.0) {
        const double h = std::max(k - std::abs(e.dome - dome), 0.0) / k;
        e.dome = std::max(e.dome, dome) + 0.25 * k * h * h;
    } else {
        e.dome = std::max(e.dome, dome);
    }
}

} // namespace terrain::coast_reefs
