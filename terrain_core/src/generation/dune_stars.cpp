// The star dunes (dune_forms.hpp: stars, starsOn, starFormOf).
//
// A star is made of its arms: each a crest drawn as a broken line of kSegments segments from its
// summit, its own length, curl and heights (a saddle and a secondary summit on some, a crest that
// rises and falls), its slip face changing side along it. The ground of a segment is exactly the
// highest of the cones standing on its crest, each tilted toward the slip face: its slope never
// exceeds kLeeTan, whatever the crest, and it never steps. The free stars spread at their foot.
#include "dune_forms.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace terrain::dune_forms {

namespace {

constexpr double kNone = -1.0e30;
// A star is on its cell where the occupancy passes its draw (within kPresenceSoft); its peak
// within kStarJitter of the center of its cell, kStarLow to kStarLow + kStarSpread of the height
// (at most kStarMost meters); its summit where its arms meet (no cone of its own: the bays between
// its arms reach up to it).
constexpr double kPresenceSoft = 0.12;
constexpr double kStarJitter = 0.6;
constexpr double kStarLow = 0.35;
constexpr double kStarSpread = 0.85;
constexpr double kStarMost = 300.0;
// The arms: 3 to 5 at uneven angles (kArmTurn of their share of the turn), in radii of the peak
// (peak / kStarTan); their crests from kArmTop to kArmTop + kArmTopSpread of the peak at the
// summit, falling as (1 - u)^kArmCurve along them (u from 0 to 1) to the floor at their end.
constexpr int kSegments = 6;
constexpr int kMostArms = 5;
constexpr double kArmTurn = 0.6;
constexpr double kArmTop = 0.85;
constexpr double kArmTopSpread = 0.15;
constexpr double kArmCurve = 1.1;
// Each arm curls by kCurlLow to kCurlLow + kCurlSpread of a turn along it, either way, with a
// jitter of kKink of a turn at each joint; its crest wobbles by kWobble of its top and its slip
// face changes side at a joint with the chance kFlip. A secondary summit (a saddle of kDip, then
// a rise of kRise of the top) stands on some arms (kSecondChance), always on the longest arm of
// the stars over kBigStar of their height.
constexpr double kCurlLow = 0.08;
constexpr double kCurlSpread = 0.08;
constexpr double kKink = 0.02;
constexpr double kWobble = 0.08;
constexpr double kFlip = 0.35;
constexpr double kSecondChance = 0.45;
constexpr double kBigStar = 0.7;
constexpr double kDipLow = 0.05;
constexpr double kDipSpread = 0.07;
constexpr double kRiseLow = 0.04;
constexpr double kRiseSpread = 0.06;
// The cones on the crests: kConeTan around them, tilted by kTilt toward the slip face (that face
// at kConeTan + kTilt = kLeeTan, the other at kConeTan - kTilt); the crest itself never steeper
// than kCrestShare of kConeTan. The stars of the chains (on a pedestal): shorter arms, untilted,
// both flanks at kLeeTan like the slip faces of their chains.
constexpr double kTilt = 0.1;
constexpr double kConeTan = kLeeTan - kTilt;
constexpr double kCrestShare = 0.5;
// The free stars spread at their foot over kFoot of their peak on each side of the floor.
constexpr double kFoot = 0.05;
// Bounds of the reach: from its summit, a star reaches at most kReach peaks (its arms at most
// kArmLow + kArmSpread radii long, their heights capped so).
struct ArmKind {
    double low;
    double spread;
    double tilt;
    double cone;
};
constexpr ArmKind kFree{1.6, 2.4, kTilt, kConeTan};
constexpr ArmKind kOnPedestal{1.2, 1.2, 0.0, kLeeTan};
constexpr double kCrestReach = 0.2; // peaks over the farthest end of the arms, on the gentle side

double reachOf(const ArmKind& a) {
    return (a.low + a.spread) / kStarTan + kCrestReach / (a.cone - a.tilt);
}

double hermite01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double presence(double draw, double occupancy) {
    return hermite01((occupancy - (draw - kPresenceSoft)) / (2.0 * kPresenceSoft));
}

// An arm, in radii (x, z) and peaks (h) from the summit: its joints, the side of the slip face of
// each segment (+1 or -1) and a circle around its joints.
struct Arm {
    std::array<double, kSegments + 1> x{};
    std::array<double, kSegments + 1> z{};
    std::array<double, kSegments + 1> h{};
    std::array<double, kSegments> side{};
    double length = 0.0;
    double cx = 0.0;
    double cz = 0.0;
    double radius = 0.0;
    double top = 0.0;
    double high = 0.0; // the highest of its crest, peaks
};

struct Shape {
    std::uint64_t hash = 0;
    bool pedestal = false;
    bool built = false;
    int arms = 0;
    std::array<Arm, kMostArms> arm{};
};

// The heights of the crest of an arm: concave, wobbling, a saddle and a secondary summit if
// second; lowered as a whole where it would be steeper than kCrestShare of kConeTan (its shape
// kept: never clipped to a straight line), capped so that the arm stays within reach.
void crestOf(Arm& a, std::uint64_t ka, bool second, const ArmKind& kind) {
    const double step = a.length / kSegments / kStarTan; // peaks
    for (int i = 1; i <= kSegments; ++i) {
        const double u = static_cast<double>(i) / kSegments;
        // Up and down from one joint to the next, by half kWobble at least: the crest never runs
        // straight.
        const double wobble = kWobble * (0.5 + 0.5 * uniform(noise::deriveSeed(ka, 40 + i))) *
                              ((i + static_cast<int>(ka & 1U)) % 2 == 0 ? 1.0 : -1.0);
        a.h[i] = i == kSegments ? 0.0 : a.top * (dmath::pow(1.0 - u, kArmCurve) + wobble * (1.0 - u));
    }
    if (second) {
        const auto j = static_cast<std::size_t>(2 + noise::deriveSeed(ka, 7) % 3);
        a.h[j - 1] -= a.top * (kDipLow + kDipSpread * uniform(noise::deriveSeed(ka, 8)));
        a.h[j] = std::max(a.h[j], a.h[j - 1] + a.top * (kRiseLow + kRiseSpread *
                                                          uniform(noise::deriveSeed(ka, 9))));
        a.h[j + 1] = std::min(a.h[j + 1], a.h[j] - 0.5 * kRiseLow * a.top);
    }
    double steepest = 0.0;
    for (std::size_t k = 1; k < a.h.size(); ++k) {
        steepest = std::max(steepest, std::abs(a.h[k] - a.h[k - 1]));
    }
    const double scale = std::min(1.0, kCrestShare * kConeTan * step / steepest);
    const double reach = reachOf(kind);
    for (std::size_t k = 0; k < a.h.size(); ++k) {
        const double far = dmath::hypot(a.x[k], a.z[k]) / kStarTan;
        a.h[k] = std::clamp(a.h[k] * scale, 0.0, (reach - far) * (kind.cone - kind.tilt));
    }
    a.top = a.h[0];
    a.high = *std::max_element(a.h.begin(), a.h.end());
}

// An arm of the star of hash h: k of arms, longest whether it is its longest.
void armOf(Arm& a, std::uint64_t h, int k, int arms, bool big, const ArmKind& kind) {
    const std::uint64_t ka = noise::deriveSeed(h, 10 + static_cast<std::uint64_t>(k));
    double heading = uniform(noise::deriveSeed(h, 5)) +
                     (static_cast<double>(k) + kArmTurn * (uniform(ka) - 0.5)) / arms;
    a.length = kind.low + kind.spread * uniform(noise::deriveSeed(ka, 1));
    const double curl = (kCurlLow + kCurlSpread * uniform(noise::deriveSeed(ka, 2))) *
                        ((noise::deriveSeed(ka, 3) & 1U) != 0U ? 1.0 : -1.0);
    a.top = kArmTop + kArmTopSpread * uniform(noise::deriveSeed(ka, 4));
    a.h[0] = a.top;
    double side = (noise::deriveSeed(ka, 5) & 1U) != 0U ? 1.0 : -1.0;
    const double step = a.length / kSegments;
    for (int i = 1; i <= kSegments; ++i) {
        const auto n = static_cast<std::size_t>(i);
        heading += curl / kSegments + kKink * (2.0 * uniform(noise::deriveSeed(ka, 20 + i)) - 1.0);
        const dmath::SineCosine d = dmath::sinCosTurns(heading);
        a.x[n] = a.x[n - 1] + step * d.cosine;
        a.z[n] = a.z[n - 1] + step * d.sine;
        a.side[n - 1] = side;
        side = uniform(noise::deriveSeed(ka, 60 + i)) < kFlip ? -side : side;
    }
    crestOf(a, ka, big || uniform(noise::deriveSeed(ka, 6)) < kSecondChance, kind);
    a.cx = 0.5 * (a.x[0] + a.x[kSegments]);
    a.cz = 0.5 * (a.z[0] + a.z[kSegments]);
    for (int i = 0; i <= kSegments; ++i) {
        const auto n = static_cast<std::size_t>(i);
        a.radius = std::max(a.radius, dmath::hypot(a.x[n] - a.cx, a.z[n] - a.cz));
    }
}

void build(Shape& s, std::uint64_t h, bool pedestal) {
    s = Shape{};
    s.hash = h;
    s.pedestal = pedestal;
    s.built = true;
    const ArmKind& kind = pedestal ? kOnPedestal : kFree;
    s.arms = 3 + static_cast<int>(noise::deriveSeed(h, 4) % 3);
    std::array<double, kMostArms> lengths{};
    int longest = 0;
    for (int k = 0; k < s.arms; ++k) {
        const std::uint64_t ka = noise::deriveSeed(h, 10 + static_cast<std::uint64_t>(k));
        lengths[static_cast<std::size_t>(k)] = uniform(noise::deriveSeed(ka, 1));
        longest = lengths[static_cast<std::size_t>(k)] >
                          lengths[static_cast<std::size_t>(longest)] ? k : longest;
    }
    const bool big = kStarLow + kStarSpread * uniform(noise::deriveSeed(h, 3)) > kBigStar;
    for (int k = 0; k < s.arms; ++k) {
        armOf(s.arm[static_cast<std::size_t>(k)], h, k, s.arms, big && k == longest, kind);
    }
}

// The shapes of the stars met last on the calling thread (the same bits whatever it holds).
const Shape& shapeOf(std::uint64_t h, bool pedestal) {
    thread_local std::array<Shape, 64> cache{};
    Shape& s = cache[(h ^ (pedestal ? 0x9E37U : 0U)) % cache.size()];
    if (!s.built || s.hash != h || s.pedestal != pedestal) {
        build(s, h, pedestal);
    }
    return s;
}

// The ground of a crest segment from a to b (x, z meters, height) at (px, pz): the highest of the
// cones of slope cone standing on it (in closed form), less tilt times the offset across it
// (its slip face on the side of the positive offsets).
double segmentAt(double ax, double az, double ah, double bx, double bz, double bh, double px,
                 double pz, double cone, double tilt) {
    const double dx = bx - ax;
    const double dz = bz - az;
    const double length = dmath::hypot(dx, dz);
    const double tx = dx / length;
    const double tz = dz / length;
    const double along = (px - ax) * tx + (pz - az) * tz;
    const double across = (px - ax) * tz - (pz - az) * tx;
    const double g = (bh - ah) / length;
    const double s = std::clamp(
        along + std::abs(across) * g / std::sqrt(cone * cone - g * g), 0.0, length);
    return ah + g * s - cone * dmath::hypot(s - along, across) - tilt * across;
}

// The foot of a free star: v smoothly spread over foot meters about the floor (slopes no steeper).
double footOf(double v, double foot) {
    if (v >= foot) {
        return v;
    }
    return v <= -foot ? 0.0 : (v + foot) * (v + foot) / (4.0 * foot);
}

// The ground of a star of shape sh, peak meters high, at (ox, oz) from its summit; below reach
// meters under it where it reaches no farther (kNone).
double groundOf(const Shape& sh, double peak, double depth, double ox, double oz) {
    const ArmKind& kind = sh.pedestal ? kOnPedestal : kFree;
    const double radius = peak / kStarTan;
    double best = kNone;
    const double slack = (depth + kFoot * peak) / (kind.cone - kind.tilt);
    for (int k = 0; k < sh.arms; ++k) {
        const Arm& a = sh.arm[static_cast<std::size_t>(k)];
        if (!(dmath::hypot(ox - a.cx * radius, oz - a.cz * radius) <
              a.radius * radius + a.high * peak / (kind.cone - kind.tilt) + slack)) {
            continue;
        }
        for (int i = 0; i < kSegments; ++i) {
            const auto n = static_cast<std::size_t>(i);
            best = std::max(best, segmentAt(a.x[n] * radius, a.z[n] * radius, a.h[n] * peak,
                                            a.x[n + 1] * radius, a.z[n + 1] * radius,
                                            a.h[n + 1] * peak, ox, oz, kind.cone, kind.tilt * a.side[n]));
        }
    }
    return best;
}

struct Center {
    std::uint64_t hash = 0;
    double there = 0.0;
    double x = 0.0;
    double z = 0.0;
    double size = 0.0; // of the height
};

Center centerOf(const StarLattice& s, std::int64_t i, std::int64_t j, std::int64_t wi,
                std::int64_t wj) {
    Center c;
    c.hash = cellHash(s.key, wi, wj);
    c.there = presence(uniform(c.hash), s.occupancy);
    c.x = (static_cast<double>(i) + 0.5 + kStarJitter * (uniform(noise::deriveSeed(c.hash, 1)) - 0.5)) *
              s.cell - 0.5 * s.periodX;
    c.z = (static_cast<double>(j) + 0.5 + kStarJitter * (uniform(noise::deriveSeed(c.hash, 2)) - 0.5)) *
              s.cell - 0.5 * s.periodZ;
    c.size = kStarLow + kStarSpread * uniform(noise::deriveSeed(c.hash, 3));
    return c;
}

// How far from its summit a star of this peak reaches down to depth under its foot.
double reachOf(double peak, double depth, bool pedestal) {
    const ArmKind& kind = pedestal ? kOnPedestal : kFree;
    return peak * (reachOf(kind) + kFoot / (kind.cone - kind.tilt)) + depth / (kind.cone - kind.tilt);
}

// One star of the cell (i, j) (wrapped as wi, wj for its draws) at (x, z).
double starOf(const StarLattice& s, std::int64_t i, std::int64_t j, std::int64_t wi,
              std::int64_t wj, double x, double z,
              const std::function<Pedestal(double, double)>* pedestal) {
    const double none = pedestal != nullptr ? kNone : 0.0;
    const Center c = centerOf(s, i, j, wi, wj);
    if (!(c.there > 0.0)) {
        return none;
    }
    const double ox = x - c.x;
    const double oz = z - c.z;
    double peak = std::min(s.height * c.size, kStarMost) * c.there;
    if (!(dmath::hypot(ox, oz) < reachOf(peak, s.depth, pedestal != nullptr))) {
        return none;
    }
    if (pedestal == nullptr) {
        const double ground = groundOf(shapeOf(c.hash, false), peak, 0.0, ox, oz);
        return footOf(ground, kFoot * peak);
    }
    const Pedestal under = (*pedestal)(c.x, c.z);
    peak = std::min(under.height * c.size, kStarMost) * c.there * under.presence;
    const double best = groundOf(shapeOf(c.hash, true), peak, under.depth, ox, oz);
    // A star fading off its pedestal sinks under it.
    return under.base + best - (1.0 - under.presence) * under.depth;
}

// The stars of the cells around (x, z) whose stars may reach it, the highest.
double starsAround(const StarLattice& s, double x, double z, double none,
                   const std::function<Pedestal(double, double)>* pedestal) {
    const auto nx = static_cast<std::int64_t>(std::floor(s.periodX / s.cell + 0.5));
    const auto nz = static_cast<std::int64_t>(std::floor(s.periodZ / s.cell + 0.5));
    const dmath::FloorIndex gi = dmath::floorIndex((x + 0.5 * s.periodX) / s.cell);
    const dmath::FloorIndex gj = dmath::floorIndex((z + 0.5 * s.periodZ) / s.cell);
    // A center lies at least (k - 1 + (1 - kStarJitter) / 2) cells from a point k cells away.
    const double reach = reachOf(std::min(s.height * (kStarLow + kStarSpread), kStarMost), s.depth,
                                 pedestal != nullptr);
    const auto span = static_cast<std::int64_t>(
        std::ceil(reach / s.cell + 0.5 + 0.5 * kStarJitter) - 1.0);
    double best = none;
    for (std::int64_t dj = -span; dj <= span; ++dj) {
        for (std::int64_t di = -span; di <= span; ++di) {
            const std::int64_t i = gi.index + di;
            const std::int64_t j = gj.index + dj;
            best = std::max(best, starOf(s, i, j, ((i % nx) + nx) % nx, ((j % nz) + nz) % nz, x, z,
                                         pedestal));
        }
    }
    return best;
}

} // namespace

double stars(const StarLattice& s, double x, double z) {
    if (!(s.occupancy > 0.0) || !(s.height > 0.0)) {
        return 0.0;
    }
    return starsAround(s, x, z, 0.0, nullptr);
}

double starsOn(const StarLattice& s, double x, double z,
               const std::function<Pedestal(double, double)>& pedestal) {
    if (!(s.occupancy > 0.0) || !(s.height > 0.0)) {
        return -1.0e30;
    }
    return starsAround(s, x, z, -1.0e30, &pedestal);
}

StarForm starFormOf(const StarLattice& s, std::int64_t i, std::int64_t j) {
    StarForm out;
    const auto nx = static_cast<std::int64_t>(std::floor(s.periodX / s.cell + 0.5));
    const auto nz = static_cast<std::int64_t>(std::floor(s.periodZ / s.cell + 0.5));
    const Center c = centerOf(s, i, j, ((i % nx) + nx) % nx, ((j % nz) + nz) % nz);
    if (!(c.there > 0.0) || !(s.height > 0.0)) {
        return out;
    }
    out.x = c.x;
    out.z = c.z;
    out.peak = std::min(s.height * c.size, kStarMost) * c.there;
    const double radius = out.peak / kStarTan;
    const Shape& sh = shapeOf(c.hash, false);
    for (int k = 0; k < sh.arms; ++k) {
        const Arm& a = sh.arm[static_cast<std::size_t>(k)];
        std::vector<StarCrest> crest;
        for (int n = 0; n <= kSegments; ++n) {
            const auto m = static_cast<std::size_t>(n);
            crest.push_back({c.x + a.x[m] * radius, c.z + a.z[m] * radius, a.h[m] * out.peak});
        }
        out.arms.push_back(crest);
    }
    return out;
}

} // namespace terrain::dune_forms
