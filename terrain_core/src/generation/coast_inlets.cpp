#include "coast_inlets.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::coast_inlets {

namespace {

constexpr std::uint64_t kStream = 0x33D1;
constexpr double kPi = 3.14159265358979323846;

// A root is found by at most kRootSteps Newton steps of at most kRootJump meters, the gradient
// read kGradientStep meters away.
constexpr int kRootSteps = 12;
constexpr double kRootJump = 1'000.0;
constexpr double kGradientStep = 16.0;
// The axis: a point every kStep meters (by lattice), at most kMaxPoints, never beyond kCap meters
// of coast signal (clear of the inner part of the band), stopped where it comes back to the sea
// (kBack meters of signal) and kRiverClear meters plus its widest half from the rivers.
constexpr std::array<double, 2> kStep{100.0, 25.0};
constexpr std::size_t kMaxPoints = 120;
constexpr double kCap = 12'000.0;
constexpr double kBack = 200.0;
constexpr double kRiverClear = 100.0;
// The signal of a stack gives way to that of the geography kIsletReach meters beyond its rim.
constexpr double kIsletReach = 60.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

struct Gradient {
    double coast = 0.0;
    double x = 0.0;
    double z = 0.0;
};

Gradient gradientOf(const World& w, double x, double z) {
    const double c = w.coast(x, z);
    return {c, (w.coast(x + kGradientStep, z) - c) / kGradientStep,
            (w.coast(x, z + kGradientStep) - c) / kGradientStep};
}

} // namespace

Draws::Draws(const World& w, Lattice lattice, std::int64_t i, std::int64_t j)
    : state_(noise::deriveSeed(
          noise::deriveSeed(noise::deriveSeed(noise::deriveSeed(w.seed, kStream),
                                              static_cast<std::uint64_t>(lattice)),
                            static_cast<std::uint64_t>(i)),
          static_cast<std::uint64_t>(j))) {}

double Draws::next() {
    state_ = noise::deriveSeed(state_, 1);
    return static_cast<double>(state_ >> 11) * 0x1.0p-53;
}

// Moves (x, z) to target meters of coast signal; false if it does not get there.
bool rootOf(const World& w, double target, double& x, double& z) {
    for (int k = 0; k < kRootSteps; ++k) {
        const Gradient g = gradientOf(w, x, z);
        if (std::abs(g.coast - target) < 30.0) {
            return true;
        }
        const double g2 = g.x * g.x + g.z * g.z;
        if (!(g2 > 1e-6)) {
            return false;
        }
        double dx = (g.coast - target) * g.x / g2;
        double dz = (g.coast - target) * g.z / g2;
        const double jump = dmath::hypot(dx, dz);
        if (jump > kRootJump) {
            dx *= kRootJump / jump;
            dz *= kRootJump / jump;
        }
        x -= dx;
        z -= dz;
    }
    return false;
}

Shape shapeOf(InletKind kind, Draws& d) {
    const double phase = d.next();
    const double half = d.next();
    const double depth = d.next();
    const double inland = d.next();
    const double extra = d.next();
    switch (kind) {
    case InletKind::Fjord:
        return {between(300.0, 800.0, half), between(80.0, 250.0, depth),
                between(2'000.0, 4'500.0, inland), 0.02, 1'600.0, phase, 0.0};
    case InletKind::Ria:
        return {between(150.0, 450.0, half), between(6.0, 24.0, depth),
                between(1'500.0, 4'000.0, inland), 0.05, 900.0, phase, 0.0};
    case InletKind::Calanque:
        return {between(25.0, 80.0, half), between(8.0, 25.0, depth),
                between(400.0, 1'200.0, inland), 0.07, 260.0, phase,
                std::max(0.0, between(-0.8, 0.8, extra))};
    case InletKind::Cove:
        return {between(40.0, 150.0, half), between(4.0, 14.0, depth),
                between(150.0, 500.0, inland), 0.0, 1.0, phase, between(0.8, 2.2, extra)};
    }
    return {};
}

namespace {

// The widest half of an inlet of that shape, anywhere along it.
double widestOf(InletKind kind, const Shape& s) {
    switch (kind) {
    case InletKind::Calanque:
        return s.half * 1.2 * (1.0 + s.extra);
    case InletKind::Cove:
        return s.half * std::max(1.0, s.extra);
    default:
        return s.half * 1.15;
    }
}

// The half width and depth of an inlet at v, 0 at its mouth to 1 at its head (0 out at sea).
AxisPoint profileOf(InletKind kind, const Shape& s, double v) {
    double half = 0.0;
    double depth = 0.0;
    switch (kind) {
    case InletKind::Fjord:
        half = s.half * (0.55 + 0.45 * smooth(0.0, 0.15, v)) * (1.0 - smooth(0.8, 1.0, v));
        depth = s.depth * (0.25 + 0.75 * smooth(0.0, 0.2, v)) * (1.0 - 0.6 * smooth(0.85, 1.0, v));
        break;
    case InletKind::Ria:
        half = s.half * (1.0 - v) * (1.0 + 0.15 * dmath::sinTurns(2.0 * v + s.phase));
        depth = s.depth * (1.0 - 0.8 * v);
        break;
    case InletKind::Calanque:
        half = s.half * (1.0 + 0.2 * dmath::sinTurns(3.0 * v + s.phase)) *
               (1.0 + s.extra * smooth(0.7, 0.85, v)) * (1.0 - smooth(0.9, 1.0, v));
        depth = s.depth * (1.0 - 0.5 * v);
        break;
    case InletKind::Cove: {
        const double widest = s.half * s.extra;
        const double round = (v - 0.6) / 0.4;
        half = v < 0.6 ? s.half + (widest - s.half) * smooth(0.0, 0.6, v)
                       : widest * std::sqrt(std::max(0.0, 1.0 - round * round));
        depth = s.depth * (1.0 - 0.7 * smooth(0.5, 1.0, v));
        break;
    }
    }
    return {0.0, 0.0, static_cast<float>(std::max(0.0, half)), static_cast<float>(depth)};
}

// The points of an axis from (x, z) in the direction of turns: s along it, and the index of its
// mouth (the first point on the land of the geography).
struct March {
    std::vector<double> x;
    std::vector<double> z;
    std::vector<double> s;
    std::size_t mouth = 0;
    bool landed = false;
};

// The direction of the gradient g in turns, or turns where it vanishes.
double turnsOf(const Gradient& g, double turns) {
    return g.x * g.x + g.z * g.z > 1e-12 ? dmath::atan2(g.z, g.x) / (2.0 * kPi) : turns;
}

// Marches from (x, z) up to inland meters beyond the mouth, the inlet widest meters half wide at
// most, first toward turns then up the coast signal, wiggling; stops before the cap of the band,
// a return to the sea or the corridor of a river.
March marchOf(const World& w, Lattice lattice, const Shape& shape, double x, double z,
              double turns, double widest, double inland) {
    const double step = kStep[static_cast<std::size_t>(lattice)];
    March m;
    Gradient here = gradientOf(w, x, z);
    for (std::size_t k = 0; k < kMaxPoints; ++k) {
        const double s = step * static_cast<double>(k);
        if (k > 0) {
            turns = turnsOf(here, turns);
            const double angle = turns + shape.wiggle * dmath::sinTurns(s / shape.wavelength +
                                                                         shape.phase);
            const dmath::SineCosine sc = dmath::sinCosTurns(angle);
            x += step * sc.cosine;
            z += step * sc.sine;
            here = gradientOf(w, x, z);
        }
        const double c = here.coast;
        if (c > kCap || (m.landed && c < -kBack) ||
            (w.clearance && w.clearance(x, z) < widest + kRiverClear)) {
            break;
        }
        if (!m.landed && c >= 0.0) {
            m.landed = true;
            m.mouth = m.x.size();
        }
        m.x.push_back(x);
        m.z.push_back(z);
        m.s.push_back(s);
        if (m.landed && s - m.s[m.mouth] >= inland) {
            break;
        }
    }
    return m;
}

// The site of an inlet: its mouth, its widest water inland and how sheltered it is.
CoastSite siteOf(const Inlet& inlet, std::size_t mouth, const World& w) {
    CoastSite site;
    site.inlet = inlet.kind;
    std::size_t widest = mouth;
    for (std::size_t k = mouth; k < inlet.axis.size(); ++k) {
        widest = inlet.axis[k].half > inlet.axis[widest].half ? k : widest;
    }
    const AxisPoint& a = inlet.axis[widest];
    site.x = wrapCoordinate(a.x, w.size.x);
    site.z = wrapCoordinate(a.z, w.size.z);
    site.opening = 2.0f * inlet.axis[mouth].half;
    site.size = 2.0f * a.half;
    site.depth = a.depth;
    const AxisPoint& m = inlet.axis[mouth];
    double length = 0.0;
    for (std::size_t k = mouth + 1; k < inlet.axis.size(); ++k) {
        length += dmath::hypot(inlet.axis[k].x - inlet.axis[k - 1].x,
                               inlet.axis[k].z - inlet.axis[k - 1].z);
    }
    site.length = static_cast<float>(length);
    const double narrowing =
        site.size > site.opening ? 1.0 - static_cast<double>(site.opening / site.size) : 0.0;
    site.shelter = static_cast<float>(std::sqrt(narrowing) * length /
                                      (length + 0.5 * static_cast<double>(site.opening)));
    const Site at = w.site(m.x, m.z);
    site.coast = static_cast<CoastKind>(
        std::max_element(at.weights.begin(), at.weights.end()) - at.weights.begin());
    site.harbor = site.shelter >= kHarborShelter && site.depth >= kHarborDepth &&
                  site.opening >= kHarborOpening && site.opening < site.size;
    return site;
}

} // namespace

// The inlet of a kind from (x, z) toward turns, or none (an empty axis) where it would not reach
// least meters inland.
Inlet inletOf(const World& w, Lattice lattice, InletKind kind, const Shape& shape, double x,
              double z, double turns, double least) {
    Inlet inlet;
    inlet.kind = kind;
    const March m =
        marchOf(w, lattice, shape, x, z, turns, widestOf(kind, shape), shape.inland);
    if (!m.landed || m.s.back() - m.s[m.mouth] < least) {
        return inlet;
    }
    const double inland = m.s.back() - m.s[m.mouth];
    double widest = 0.0;
    for (std::size_t k = 0; k < m.x.size(); ++k) {
        const double v = k <= m.mouth ? 0.0 : (m.s[k] - m.s[m.mouth]) / inland;
        AxisPoint p = profileOf(kind, shape, v);
        p.x = m.x[k];
        p.z = m.z[k];
        inlet.axis.push_back(p);
        widest = std::max(widest, static_cast<double>(p.half));
    }
    inlet.wall = static_cast<float>(kind == InletKind::Fjord ? 1'200.0
                                    : kind == InletKind::Ria ? 1'500.0
                                                             : 300.0);
    const double reach = widest + static_cast<double>(inlet.wall);
    inlet.minX = *std::min_element(m.x.begin(), m.x.end()) - reach;
    inlet.maxX = *std::max_element(m.x.begin(), m.x.end()) + reach;
    inlet.minZ = *std::min_element(m.z.begin(), m.z.end()) - reach;
    inlet.maxZ = *std::max_element(m.z.begin(), m.z.end()) + reach;
    inlet.site = siteOf(inlet, m.mouth, w);
    return inlet;
}

namespace {

// A distance w as the signal of the inlets reads it: itself up to near, then growing to infinity
// at far, so that their signal gives way to that of the geography before far (C1 at near).
double reachOf(double w, double near, double far) {
    if (w <= near) {
        return w;
    }
    return w < far ? w + (w - near) * (w - near) / (far - w)
                   : std::numeric_limits<double>::infinity();
}

double segmentT(double px, double pz, const AxisPoint& a, const AxisPoint& b) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    const double l2 = dx * dx + dz * dz;
    return l2 > 0.0 ? std::clamp(((px - a.x) * dx + (pz - a.z) * dz) / l2, 0.0, 1.0) : 0.0;
}

// The cross section of the floor of an inlet, 0 at its water's edge to 1 on its axis (s).
double crossOf(InletKind kind, double s) {
    switch (kind) {
    case InletKind::Fjord:
        return smooth(0.0, 0.4, s);
    case InletKind::Ria:
        return smooth(0.0, 1.0, s);
    case InletKind::Calanque:
        return smooth(0.0, 0.5, s);
    case InletKind::Cove:
        return smooth(0.0, 0.7, s);
    }
    return 0.0;
}

} // namespace

// The direction up the coast signal at (x, z), in turns.
double inlandOf(const World& w, double x, double z) {
    return turnsOf(gradientOf(w, x, z), 0.0);
}

double signalOf(double coast, const Effect& e) {
    return std::max(std::min(coast, kSignalPerMeter * e.water), kSignalPerMeter * e.islet);
}

double distanceOf(double base, const Effect& e) {
    return std::max(std::min(base, e.water), e.islet);
}

void addInlet(const Inlet& inlet, double x, double z, const WorldSize& world, Effect& e) {
    const AxisPoint& root = inlet.axis.front();
    const double px = root.x + noise::wrapCanonical(x - root.x, world.x);
    const double pz = root.z + noise::wrapCanonical(z - root.z, world.z);
    if (px < inlet.minX || px > inlet.maxX || pz < inlet.minZ || pz > inlet.maxZ) {
        return;
    }
    double water = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k + 1 < inlet.axis.size(); ++k) {
        const AxisPoint& a = inlet.axis[k];
        const AxisPoint& b = inlet.axis[k + 1];
        const double t = segmentT(px, pz, a, b);
        const double half =
            static_cast<double>(a.half) + static_cast<double>(b.half - a.half) * t;
        const double away =
            dmath::hypot(px - (a.x + t * (b.x - a.x)), pz - (a.z + t * (b.z - a.z))) - half;
        water = std::min(water, away);
        if (away < 0.0 && half > 0.0) {
            const double depth =
                static_cast<double>(a.depth) + static_cast<double>(b.depth - a.depth) * t;
            e.floor = std::min(e.floor, -depth * crossOf(inlet.kind, -away / half));
            e.deep = std::max(e.deep, depth);
        }
    }
    const auto reach = static_cast<double>(inlet.wall);
    e.water = std::min(e.water, reachOf(water, 0.5 * reach, reach));
    const double wall = std::clamp(water / reach, 0.0, 1.0);
    e.rise = std::min(e.rise, wall * (2.0 - wall));
    const double sink = std::clamp(-water / reach, 0.0, 1.0);
    e.sink = std::max(e.sink, sink * (2.0 - sink));
}

void addStack(const Stack& stack, double x, double z, const WorldSize& world, Effect& e) {
    const double dx = noise::wrapCanonical(x - stack.x, world.x);
    const double dz = noise::wrapCanonical(z - stack.z, world.z);
    const auto radius = static_cast<double>(stack.radius);
    if (std::abs(dx) > radius + kIsletReach || std::abs(dz) > radius + kIsletReach) {
        return;
    }
    const double r = dmath::hypot(dx, dz);
    e.islet = std::max(e.islet, r < radius ? radius - r : -reachOf(r - radius, 0.0, kIsletReach));
    const double u = r / radius;
    e.dome = std::max(e.dome, static_cast<double>(stack.height) * std::max(0.0, 1.0 - u * u));
}

} // namespace terrain::coast_inlets

namespace terrain {

const char* inletKindName(InletKind kind) {
    switch (kind) {
    case InletKind::Fjord:
        return "fjord";
    case InletKind::Ria:
        return "ria";
    case InletKind::Calanque:
        return "calanque";
    case InletKind::Cove:
        return "cove";
    }
    return "unknown";
}

} // namespace terrain
