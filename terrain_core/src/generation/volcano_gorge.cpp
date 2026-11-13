// The breach of a caldera and the gorge it opens down the flank (volcano_gorge.hpp).
#include "volcano_gorge.hpp"

#include "terrain/det_math.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain::volcano_gorge {

namespace vf = volcano_forms;
using volcano_sites::between;

namespace {

// The draws of a gorge (apart from those of the shape, below 160) and the streams of its noises.
enum Draw : std::uint64_t {
    kLipHalf = 160,
    kLength,
    kWiden,
    kDeepen,
    kWave,
    kSwing,
    kBottom,
};

enum Stream : std::uint64_t {
    kAxis = 112,
    kAxisSwing,
    kWidth,
    kLeftWidth,
    kRightWidth,
    kLeftSteep,
    kRightSteep,
};

// The share of its half width its flat floor takes.
constexpr double kFloor = 0.3;

struct Axis {
    double offset = 0.0; // across, meters
    double cosine = 1.0; // of the angle of the axis with the line down the flank
};

// The axis d meters past the lip: straight there, then bending one way and the other, a bend every
// half wave or so, its phase and swing drifting (no two bends alike).
Axis axisAt(const VolcanoSite& s, const volcano_relief::GorgeShape& g, double d) {
    const auto offset = [&](double t) {
        if (!(t > 0.0)) {
            return 0.0;
        }
        const double ramp = vf::smoothstep(0.0, 0.5 * g.wave, t);
        const double phase = t / g.wave + 0.25 * vf::lineNoise(s.seed + kAxis, t / (2.0 * g.wave));
        const double swing =
            g.swing * (0.75 + 0.25 * vf::lineNoise(s.seed + kAxisSwing, t / (1.5 * g.wave)));
        return ramp * swing * dmath::sinTurns(phase);
    };
    Axis a;
    a.offset = offset(d);
    const double slope = (offset(d + 1.0) - offset(d - 1.0)) / 2.0;
    a.cosine = 1.0 / std::sqrt(1.0 + slope * slope);
    return a;
}

// Its half width on one side, d meters past the lip.
double halfWidth(const VolcanoSite& s, const volcano_relief::GorgeShape& g, double d, bool left) {
    const double t = std::max(d, 0.0);
    const double down = 1.0 + (g.widen - 1.0) * vf::smoothstep(0.0, 0.6 * g.length, t);
    const double along = 1.0 + 0.2 * vf::lineNoise(s.seed + kWidth, t / (0.5 * g.wave));
    const double side =
        1.0 + 0.25 * vf::lineNoise(s.seed + (left ? kLeftWidth : kRightWidth), t / (0.7 * g.wave));
    return g.lipHalf * down * along * side;
}

// The steepness of one wall (the exponent of its profile, 0.7 to 2.3) d meters past the lip.
double steepness(const VolcanoSite& s, const volcano_relief::GorgeShape& g, double d, bool left) {
    return 1.3 * dmath::exp(0.55 * vf::lineNoise(s.seed + (left ? kLeftSteep : kRightSteep),
                                                 std::max(d, 0.0) / g.wave));
}

// How deep the gorge lies under the flank d meters past its lip (land): from its depth at the lip
// to its deepest 0.4 of the way down, fading from 0.45 of the way to its end.
double depthAt(const volcano_relief::GorgeShape& g, double d) {
    const double deeper = g.lipDepth + (g.depth - g.lipDepth) *
                                           vf::smoothstep(0.0, 0.4 * g.length, d);
    return deeper * (1.0 - vf::smoothstep(0.45 * g.length, g.length, d));
}

// Its floor on land d meters past the lip (the table of the details), NaN past its end.
double floorAt(const volcano_relief::Shape& sh, double d) {
    const std::vector<float>& f = sh.gorge.floor;
    if (!(d > 0.0)) {
        return sh.breachBottom;
    }
    const double x = d / volcano_relief::kGorgeStep;
    const auto i = static_cast<std::size_t>(x);
    if (i + 1 >= f.size()) {
        return std::nan("");
    }
    const double t = x - static_cast<double>(i);
    return static_cast<double>(f[i]) + t * static_cast<double>(f[i + 1] - f[i]);
}

} // namespace

volcano_relief::GorgeShape gorgeOf(const VolcanoSite& s) {
    volcano_relief::GorgeShape g;
    const bool strait = s.form == VolcanoForm::IslandCaldera;
    const double flank =
        std::max(0.0, static_cast<double>(s.radius) - static_cast<double>(s.crater));
    g.lipHalf = strait ? between(s.seed, kLipHalf, 150.0, 400.0)
                       : between(s.seed, kLipHalf, 40.0, 120.0);
    g.length = strait ? flank : between(s.seed, kLength, 0.35, 0.6) * flank;
    g.widen = strait ? between(s.seed, kWiden, 1.5, 2.5) : between(s.seed, kWiden, 2.5, 3.5);
    g.deepen = between(s.seed, kDeepen, 1.6, 2.4);
    g.wave = strait ? between(s.seed, kWave, 500.0, 1000.0) : between(s.seed, kWave, 700.0, 1500.0);
    g.swing = (strait ? between(s.seed, kSwing, 0.08, 0.14) : between(s.seed, kSwing, 0.15, 0.22)) *
              g.wave;
    return g;
}

double cut(const VolcanoSite& s, const volcano_relief::Shape& sh, const Place& at,
           double footprint, volcano_relief::Point& p) {
    const volcano_relief::GorgeShape& g = sh.gorge;
    const double d = at.r - at.lip;
    const bool land = !(sh.breachBottom < 0.0);
    const double end = land ? static_cast<double>(g.floor.size()) * volcano_relief::kGorgeStep
                            : g.length;
    const double delta = vf::angleBetween(vf::kTwoPi * at.turns, sh.breachAngle);
    if (!(std::abs(delta) < 0.5 * vf::kPi) || !(d > -0.7 * at.lip) || !(d < end)) {
        return 0.0;
    }
    // Across the gorge, meters (along the arc): out of its reach, nothing to compute.
    const double y = at.r * delta;
    const double reach = 1.1 * g.swing + 1.6 * g.lipHalf * g.widen + footprint;
    if (!(std::abs(y) < reach)) {
        return 0.0;
    }
    const Axis axis = axisAt(s, g, d);
    const double across = (y - axis.offset) * axis.cosine;
    const bool left = across < 0.0;
    const double half = halfWidth(s, g, d, left);
    const double e = steepness(s, g, d, left);
    const double t = std::abs(across) / half;
    double share = 0.0;
    if (t < kFloor) {
        share = 1.0;
    } else if (t < 1.0) {
        share = 1.0 - dmath::pow((t - kFloor) / (1.0 - kFloor), e);
    }
    if (footprint > 0.0) {
        // Where the footprint is coarser than the gorge, its mean over it.
        const double coarse = vf::smoothstep(0.5, 2.0, footprint / (2.0 * half));
        const double width = std::max(half, 0.5 * footprint);
        const double mean = std::abs(across) < width
                                ? (kFloor + (1.0 - kFloor) * e / (e + 1.0)) * half / width
                                : 0.0;
        share += (mean - share) * coarse;
    }
    if (!(share > 0.0)) {
        return 0.0;
    }
    // Down to the bottom of the breach through the wall and, at sea, all the way; on land past the
    // lip, down to its floor.
    const bool strait = sh.breachBottom < 0.0;
    const double floor = strait ? sh.breachBottom : floorAt(sh, d);
    if (!(floor == floor)) {
        return 0.0;
    }
    p.height -= std::max(0.0, p.height - floor) * share;
    p.touched = true;
    p.flooded = p.flooded || (strait && p.height < 0.0);
    return share;
}

} // namespace terrain::volcano_gorge

namespace terrain::volcano_gorge {

namespace {

// The surface of the volcano without its breach at the point (r, a) of its frame (meters from the
// center of its crater, radians).
struct Bare {
    const VolcanoSite& s;
    volcano_relief::Detail detail;
    const volcano_relief::GroundAt& ground;

    double at(double r, double a) const {
        const volcano_relief::FrameOffset o = volcano_relief::offsetOfFrame(
            s, detail.shape, r * dmath::cos(a), r * dmath::sin(a));
        return volcano_relief::surfaceBeforeFlows(s, detail, o.dx, o.dz,
                                                  ground(s.x + o.dx, s.z + o.dz));
    }
    // The crest of the rim at the azimuth turns.
    double rim(double turns) const {
        return at(volcano_relief::craterRadiusAt(s, detail.shape, turns), vf::kTwoPi * turns);
    }
};

// The bottom of the breach on land: 0.75 to 0.92 of the way from the floor of the crater up to
// the crest at the lip, under every other point of the rim (0.05 turn away from the lip at least)
// by 0.05 of the depth, under the lip by 40 m at least.
double bottomOf(const Bare& bare, double lip) {
    const VolcanoSite& s = bare.s;
    const volcano_relief::Shape& sh = bare.detail.shape;
    const auto depth = static_cast<double>(s.craterDepth);
    const double floor =
        volcano_relief::edificeBase(s, bare.detail) +
        volcano_relief::edificeRise(s, sh) *
            volcano_relief::profileOf(s, sh, static_cast<double>(s.crater) /
                                                 static_cast<double>(s.radius)) -
        depth;
    const double at = sh.breachAngle / vf::kTwoPi;
    double bottom = floor + (lip - floor) * between(s.seed, kBottom, 0.75, 0.92);
    for (int k = 0; k < 180; ++k) {
        bottom = std::min(bottom, bare.rim(at + 0.05 + 0.9 * (k + 0.5) / 180.0) - 0.05 * depth);
    }
    return std::min(bottom, lip - 40.0);
}

} // namespace

void readBreach(const VolcanoSite& s, volcano_relief::Detail& d,
                const volcano_relief::GroundAt& ground) {
    volcano_relief::Shape& sh = d.shape;
    volcano_relief::GorgeShape& g = sh.gorge;
    if (s.form == VolcanoForm::IslandCaldera) {
        sh.breachBottom = -between(s.seed, kBottom, 20.0, 80.0);
        return;
    }
    Bare bare{s, {}, ground};
    bare.detail.shape = sh;
    bare.detail.shape.breached = false;
    bare.detail.base = d.base;
    const double lip = volcano_relief::craterRadiusAt(s, sh, sh.breachAngle / vf::kTwoPi);
    const double most =
        0.95 * std::max(0.0, static_cast<double>(s.radius) - static_cast<double>(s.crater));
    const auto count = static_cast<std::size_t>(most / volcano_relief::kGorgeStep) + 1;
    // The flank along the axis, without the gorge; at its lip, the crest.
    std::vector<double> flank(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double along = static_cast<double>(i) * volcano_relief::kGorgeStep;
        const double r = lip + along;
        flank[i] = bare.at(r, sh.breachAngle + axisAt(s, g, along).offset / r);
    }
    sh.breachBottom = bottomOf(bare, flank[0]);
    g.lipDepth = flank[0] - sh.breachBottom;
    g.depth = std::max(g.lipDepth,
                       std::min(g.lipDepth * g.deepen, 0.6 * static_cast<double>(s.craterDepth)));
    for (int attempt = 0; attempt < 4; ++attempt) {
        g.floor.assign(1, static_cast<float>(sh.breachBottom));
        double floor = sh.breachBottom;
        for (std::size_t i = 1; i < count; ++i) {
            const double along = static_cast<double>(i) * volcano_relief::kGorgeStep;
            floor = std::min(floor, flank[i] - depthAt(g, along));
            g.floor.push_back(static_cast<float>(floor));
            if (along >= g.length && flank[i] <= floor + 1.0) {
                return;
            }
        }
        g.depth = std::max(g.lipDepth, 0.75 * g.depth);
    }
}

} // namespace terrain::volcano_gorge
