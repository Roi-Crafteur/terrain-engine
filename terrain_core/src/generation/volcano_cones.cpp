// The small forms of a volcano (volcano_cones.hpp), drawn from its seed. Sizes from real ones:
// cinder cones 40 to 300 m high, their height about 0.18 of their foot, their crater 0.4 of it,
// a third of them breached (Paricutin, the Chaine des Puys); spatter cones of 10 to 60 m along
// fissures (Laki); maars of 150 m to 1 km in radius, their floor 25 to 200 m under the ground,
// their ring of tuff 10 to 30 m (the Eifel); a cone in a caldera (Wizard Island: 230 m in a
// caldera of 600 m), a dome in the amphitheater of a collapse (Mount St. Helens), a resurgent
// dome in a caldera of a plateau. Flows 3 to 40 m thick (60 m for the domes of a plateau), 100 m
// to 1.2 km wide.
#include "volcano_cones.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain::volcano_cones {

namespace vf = volcano_forms;
using volcano_relief::Cone;
using volcano_relief::Detail;
using volcano_relief::Flow;
using volcano_relief::Shape;
using volcano_sites::between;
using volcano_sites::unit;

namespace {

using F = VolcanoForm;

// The draws of the site for its counts.
enum Draw : std::uint64_t {
    kSatellites = 40,
    kInner,
    kFieldCount,
    kLines,
    kFlows,
};

struct Offset {
    double dx = 0.0;
    double dz = 0.0;
};

// The offset from the center of the point at rho (radii of the foot) and turns in the frame of
// the volcano.
Offset offsetAt(const VolcanoSite& s, const Shape& sh, double rho, double turns) {
    const double r = rho * static_cast<double>(s.radius);
    const double u = r * dmath::cosTurns(turns) * static_cast<double>(s.elongation);
    const double v = r * dmath::sinTurns(turns);
    return {u * sh.cosAxis - v * sh.sinAxis, u * sh.sinAxis + v * sh.cosAxis};
}

// The summit in the same coordinates (Shape::offsetU, offsetV in the stretched frame).
Offset summitOf(const VolcanoSite& s, const Shape& sh) {
    const double u = sh.offsetU * static_cast<double>(s.elongation);
    return {u * sh.cosAxis - sh.offsetV * sh.sinAxis, u * sh.sinAxis + sh.offsetV * sh.cosAxis};
}

int countIn(std::uint64_t seed, std::uint64_t index, int least, int most) {
    return least + static_cast<int>(std::floor(unit(seed, index) * (most - least + 1)));
}

void keep(const VolcanoSite& s, Detail& d, Cone c) {
    if (dmath::hypot(c.x, c.z) + c.reach < static_cast<double>(s.reach) - 100.0) {
        d.cones.push_back(c);
    }
}

// A cinder cone of height h at (x, z), its sizes drawn from seed.
Cone cinder(std::uint64_t seed, double x, double z, double h) {
    Cone c;
    c.x = x;
    c.z = z;
    c.height = h;
    c.radius = h / 0.36 * between(seed, 1, 0.9, 1.15);
    c.crater = 0.4 * c.radius * between(seed, 2, 0.8, 1.05);
    c.depth = h * between(seed, 3, 0.25, 0.5);
    c.breached = unit(seed, 4) < 0.35;
    c.breachAngle = vf::kTwoPi * unit(seed, 5);
    c.reach = c.radius;
    return c;
}

// A dome (no crater) of height h and foot radius at (x, z).
Cone dome(double x, double z, double h, double radius) {
    Cone c;
    c.x = x;
    c.z = z;
    c.height = h;
    c.radius = radius;
    c.reach = radius;
    return c;
}

std::uint64_t coneSeed(const VolcanoSite& s, std::size_t i) {
    return noise::deriveSeed(s.seed, 1000 + i);
}

void satellites(const VolcanoSite& s, Detail& d) {
    int most = 0;
    int least = 0;
    double low = 40.0;
    double high = 200.0;
    switch (s.form) {
    case F::Strato:
    case F::TwinStrato:
    case F::CollapsedStrato:
        most = 12;
        break;
    case F::Caldera:
        most = 8;
        break;
    case F::Shield:
        least = 2;
        most = 15;
        low = 20.0;
        high = 120.0;
        break;
    case F::IslandShield:
        most = 10;
        low = 20.0;
        high = 120.0;
        break;
    case F::IslandStrato:
        most = 4;
        break;
    case F::PlateauCaldera:
        most = 6;
        break;
    default:
        return;
    }
    const bool rifts = volcano_sites::formRule(s.form).shield;
    const int n = countIn(s.seed, kSatellites, least, most);
    for (int i = 0; i < n; ++i) {
        const std::uint64_t seed = coneSeed(s, static_cast<std::size_t>(i));
        // Along the rift zones of a shield (its long axis), anywhere on the flanks of the others.
        const double turns = rifts ? (unit(seed, 10) < 0.5 ? 0.0 : 0.5) + 0.08 * (unit(seed, 11) - 0.5)
                                   : unit(seed, 11);
        const double rho = rifts ? between(seed, 12, 0.3, 1.1) : between(seed, 12, 0.35, 1.25);
        const Offset o = offsetAt(s, d.shape, rho, turns);
        const double u = unit(seed, 13);
        keep(s, d, cinder(seed, o.dx, o.dz, low + (high - low) * u * u));
    }
}

void inner(const VolcanoSite& s, Detail& d) {
    const std::uint64_t seed = coneSeed(s, 900);
    const double draw = unit(s.seed, kInner);
    const Offset top = summitOf(s, d.shape);
    const auto crater = static_cast<double>(s.crater);
    const double angle = vf::kTwoPi * unit(seed, 20);
    const double off = between(seed, 21, 0.0, 0.35) * crater;
    const double x = top.dx + off * dmath::cos(angle);
    const double z = top.dz + off * dmath::sin(angle);
    if ((s.form == F::Caldera && draw < 0.4) || (s.form == F::IslandCaldera && draw < 0.5)) {
        const double share = s.form == F::Caldera ? between(seed, 22, 0.2, 0.45)
                                                  : between(seed, 22, 0.3, 0.7);
        const double h = share * static_cast<double>(s.craterDepth);
        Cone c = cinder(seed, x, z, h);
        c.radius = h / 0.4;
        c.crater = 0.35 * c.radius;
        c.depth = 0.3 * h;
        c.reach = c.radius;
        keep(s, d, c);
    } else if (s.form == F::PlateauCaldera && draw < 0.5) {
        keep(s, d, dome(x, z, between(seed, 22, 100.0, 300.0),
                        crater * between(seed, 23, 0.3, 0.5)));
    } else if (s.form == F::CollapsedStrato && draw < 0.7) {
        const double a = 0.35 * crater;
        const double h = between(seed, 22, 80.0, 250.0);
        keep(s, d, dome(top.dx + a * dmath::cos(d.shape.collapseAngle + static_cast<double>(s.axis)),
                        top.dz + a * dmath::sin(d.shape.collapseAngle + static_cast<double>(s.axis)),
                        h, h / 0.35));
    }
}

// The cones of a field of cinder cones: most along a few fissures, the others scattered.
void cinderField(const VolcanoSite& s, Detail& d) {
    const double area = vf::kPi * static_cast<double>(s.radius) * static_cast<double>(s.radius) *
                        static_cast<double>(s.elongation);
    const int n = std::clamp(
        static_cast<int>(std::floor(area / 4.0e6 * between(s.seed, kFieldCount, 0.5, 1.3) + 0.5)),
        10, 80);
    const int lines = 2 + static_cast<int>(4.0 * unit(s.seed, kLines));
    const double top = static_cast<double>(s.height);
    for (int i = 0; i < n; ++i) {
        const std::uint64_t seed = coneSeed(s, static_cast<std::size_t>(i));
        Offset o;
        if (unit(seed, 10) < 0.7) {
            const std::uint64_t line = noise::deriveSeed(s.seed, 3000 + i % lines);
            const Offset mid = offsetAt(s, d.shape, between(line, 1, 0.0, 0.6), unit(line, 2));
            const double dir = static_cast<double>(s.axis) + 0.44 * (unit(line, 3) - 0.5);
            const double half = between(line, 4, 0.3, 0.9) * static_cast<double>(s.radius) *
                                static_cast<double>(s.elongation);
            const double t = between(seed, 11, -1.0, 1.0) * half;
            const double side = between(seed, 12, -200.0, 200.0);
            o = {mid.dx + t * dmath::cos(dir) - side * dmath::sin(dir),
                 mid.dz + t * dmath::sin(dir) + side * dmath::cos(dir)};
        } else {
            o = offsetAt(s, d.shape, 0.95 * std::sqrt(unit(seed, 11)), unit(seed, 12));
        }
        const double u = unit(seed, 13);
        keep(s, d, cinder(seed, o.dx, o.dz, 40.0 + (top - 40.0) * u * u));
    }
}

void lavaField(const VolcanoSite& s, Detail& d) {
    const int n = countIn(s.seed, kFieldCount, 5, 30);
    for (int i = 0; i < n; ++i) {
        const std::uint64_t seed = coneSeed(s, static_cast<std::size_t>(i));
        const Offset o = offsetAt(s, d.shape, 0.9 * std::sqrt(unit(seed, 11)), unit(seed, 12));
        const double u = unit(seed, 13);
        keep(s, d, cinder(seed, o.dx, o.dz, 20.0 + 100.0 * u * u));
    }
}

// Spatter cones and craters along the axis of a row of fissures.
void fissureRow(const VolcanoSite& s, Detail& d) {
    const int n = countIn(s.seed, kFieldCount, 10, 40);
    const double half = 0.85 * static_cast<double>(s.radius) * static_cast<double>(s.elongation);
    for (int i = 0; i < n; ++i) {
        const std::uint64_t seed = coneSeed(s, static_cast<std::size_t>(i));
        const double t = between(seed, 11, -1.0, 1.0) * half;
        const double side = between(seed, 12, -200.0, 200.0) * unit(seed, 14);
        const double u = unit(seed, 13);
        const double h = unit(seed, 15) < 0.1 ? between(seed, 16, 60.0, 150.0) : 10.0 + 50.0 * u * u;
        Cone c = cinder(seed, t * d.shape.cosAxis - side * d.shape.sinAxis,
                        t * d.shape.sinAxis + side * d.shape.cosAxis, h);
        c.depth = 0.5 * h;
        c.breached = unit(seed, 4) < 0.5;
        c.breachAngle = static_cast<double>(s.axis) + (unit(seed, 5) < 0.5 ? 0.0 : vf::kPi);
        keep(s, d, c);
    }
}

// Maars kept apart, and a few cinder cones.
void maarField(const VolcanoSite& s, Detail& d) {
    const int maars = countIn(s.seed, kFieldCount, 3, 12);
    for (int i = 0; i < maars; ++i) {
        const std::uint64_t seed = coneSeed(s, static_cast<std::size_t>(i));
        const double u = unit(seed, 10);
        Cone c;
        c.maar = true;
        c.radius = 150.0 + 850.0 * u * u;
        c.height = static_cast<double>(s.height) * between(seed, 11, 0.4, 1.0);
        c.ring = between(seed, 12, 10.0, 30.0);
        c.reach = 2.2 * c.radius;
        for (std::uint64_t attempt = 0; attempt < 24; ++attempt) {
            const Offset o = offsetAt(s, d.shape, 0.9 * std::sqrt(unit(seed, 20 + 2 * attempt)),
                                      unit(seed, 21 + 2 * attempt));
            const bool apart = std::none_of(d.cones.begin(), d.cones.end(), [&](const Cone& other) {
                return dmath::hypot(o.dx - other.x, o.dz - other.z) <
                       0.9 * (c.radius + other.radius);
            });
            if (apart) {
                c.x = o.dx;
                c.z = o.dz;
                keep(s, d, c);
                break;
            }
        }
    }
    const int cones = countIn(s.seed, kLines, 2, 8);
    for (int i = 0; i < cones; ++i) {
        const std::uint64_t seed = coneSeed(s, static_cast<std::size_t>(100 + i));
        const Offset o = offsetAt(s, d.shape, 0.9 * std::sqrt(unit(seed, 11)), unit(seed, 12));
        keep(s, d, cinder(seed, o.dx, o.dz, between(seed, 13, 50.0, 150.0)));
    }
}

} // namespace

void addCones(const VolcanoSite& s, Detail& d) {
    switch (s.form) {
    case F::CinderField:
        cinderField(s, d);
        break;
    case F::LavaField:
        lavaField(s, d);
        break;
    case F::FissureRow:
        fissureRow(s, d);
        break;
    case F::MaarField:
        maarField(s, d);
        break;
    default:
        satellites(s, d);
        inner(s, d);
        break;
    }
}

namespace {

struct FlowRule {
    int least = 0;
    int most = 0;
    double rhoEnd[2] = {0.8, 1.4};
    double width[2] = {150.0, 600.0};
    double thickness[2] = {10.0, 40.0};
};

FlowRule flowRuleOf(const VolcanoSite& s) {
    FlowRule r;
    switch (s.form) {
    case F::Strato:
    case F::TwinStrato:
        r.least = s.active ? 2 : 1;
        r.most = s.active ? 6 : 4;
        break;
    case F::CollapsedStrato:
    case F::Caldera:
        r.least = 1;
        r.most = 3;
        break;
    case F::IslandStrato:
        r.least = 1;
        r.most = 4;
        break;
    case F::IslandCaldera:
        r.most = 2;
        break;
    case F::Shield:
    case F::IslandShield:
        r.least = s.active || s.form == F::IslandShield ? 4 : 2;
        r.most = s.active || s.form == F::IslandShield ? 10 : 6;
        r.rhoEnd[0] = 0.6;
        r.rhoEnd[1] = 1.2;
        r.width[1] = 1200.0;
        r.thickness[0] = 3.0;
        r.thickness[1] = 12.0;
        break;
    case F::PlateauCaldera:
        r.most = 2;
        r.rhoEnd[0] = 0.4;
        r.rhoEnd[1] = 0.9;
        r.width[0] = 400.0;
        r.width[1] = 1200.0;
        r.thickness[0] = 30.0;
        r.thickness[1] = 60.0;
        break;
    default:
        break;
    }
    return r;
}

// A flow from (x, z) toward the angle dir, its sizes drawn in the ranges.
Flow flowFrom(std::uint64_t seed, double x, double z, double dir, double length, double width,
              double thickness) {
    Flow f;
    f.x = x;
    f.z = z;
    f.dirX = dmath::cos(dir);
    f.dirZ = dmath::sin(dir);
    f.length = length;
    f.width = width;
    f.thickness = thickness;
    f.meander = between(seed, 30, 0.5 * width, 1.2 * width);
    f.seed = seed;
    return f;
}

// The flows of an edifice: from near its summit, down its flank and out over its apron.
void edificeFlows(const VolcanoSite& s, Detail& d) {
    const FlowRule rule = flowRuleOf(s);
    const int n = rule.most > 0 ? countIn(s.seed, kFlows, rule.least, rule.most) : 0;
    const double radius = static_cast<double>(s.radius);
    const double craterRho = static_cast<double>(s.crater) / radius;
    for (int i = 0; i < n; ++i) {
        const std::uint64_t seed = noise::deriveSeed(s.seed, 5000 + static_cast<std::uint64_t>(i));
        const double turns = unit(seed, 1);
        const double start = between(seed, 2, std::max(1.2 * craterRho, 0.03), 0.35);
        const double end = between(seed, 3, rule.rhoEnd[0], rule.rhoEnd[1]);
        const Offset o = offsetAt(s, d.shape, start, turns);
        const Offset tip = offsetAt(s, d.shape, end, turns);
        const double length = dmath::hypot(tip.dx - o.dx, tip.dz - o.dz);
        d.flows.push_back(flowFrom(seed, o.dx, o.dz, dmath::atan2(tip.dz - o.dz, tip.dx - o.dx),
                                   length, between(seed, 4, rule.width[0], rule.width[1]),
                                   between(seed, 5, rule.thickness[0], rule.thickness[1])));
    }
}

void fieldFlows(const VolcanoSite& s, Detail& d) {
    if (s.form == F::CinderField) {
        for (std::size_t i = 0; i < d.cones.size(); ++i) {
            const std::uint64_t seed = noise::deriveSeed(s.seed, 5000 + i);
            if (!(unit(seed, 1) < 0.3)) {
                continue;
            }
            d.flows.push_back(flowFrom(seed, d.cones[i].x, d.cones[i].z,
                                       vf::kTwoPi * unit(seed, 2), between(seed, 3, 1000.0, 6000.0),
                                       between(seed, 4, 100.0, 400.0),
                                       between(seed, 5, 4.0, 15.0)));
        }
    } else if (s.form == F::LavaField) {
        const int n = countIn(s.seed, kFlows, 2, 6);
        for (int i = 0; i < n; ++i) {
            const std::uint64_t seed =
                noise::deriveSeed(s.seed, 5000 + static_cast<std::uint64_t>(i));
            const Offset o = offsetAt(s, d.shape, 0.6 * std::sqrt(unit(seed, 1)), unit(seed, 2));
            d.flows.push_back(flowFrom(seed, o.dx, o.dz, vf::kTwoPi * unit(seed, 3),
                                       between(seed, 4, 2000.0, 8000.0),
                                       between(seed, 5, 200.0, 700.0),
                                       static_cast<double>(s.height) * between(seed, 6, 0.3, 0.7)));
        }
    } else if (s.form == F::FissureRow && !d.cones.empty()) {
        const int n = countIn(s.seed, kFlows, 2, 5);
        for (int i = 0; i < n; ++i) {
            const std::uint64_t seed =
                noise::deriveSeed(s.seed, 5000 + static_cast<std::uint64_t>(i));
            const auto count = static_cast<double>(d.cones.size());
            const Cone& c =
                d.cones[static_cast<std::size_t>(unit(seed, 1) * count) % d.cones.size()];
            const double side = unit(seed, 2) < 0.5 ? 0.5 * vf::kPi : -0.5 * vf::kPi;
            const double dir = static_cast<double>(s.axis) + side + 0.52 * (unit(seed, 3) - 0.5);
            d.flows.push_back(flowFrom(seed, c.x, c.z, dir, between(seed, 4, 2000.0, 10'000.0),
                                       between(seed, 5, 200.0, 800.0), between(seed, 6, 5.0, 20.0)));
        }
    }
}

} // namespace

void addFlows(const VolcanoSite& s, Detail& d) {
    if (volcano_sites::formRule(s.form).edifice) {
        edificeFlows(s, d);
    } else {
        fieldFlows(s, d);
    }
}

} // namespace terrain::volcano_cones
