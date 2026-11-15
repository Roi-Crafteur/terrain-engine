// The springs of the active volcanoes (volcano_water.hpp): a zone on the floor of a caldera, by the
// crater of a cone or of a shield, in a field of lava; in it hot springs on their terraces of
// sinter (3 to 25 m), geysers on their mounds, mud pots and fumaroles, on gentle ground above the
// sea and out of the lakes. Their pools hold water at the level of the ground they were dug in.
#include "volcano_water.hpp"

#include "terrain/det_math.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain {

const char* geothermalKindName(GeothermalKind kind) {
    switch (kind) {
    case GeothermalKind::HotSpring:
        return "hot spring";
    case GeothermalKind::Geyser:
        return "geyser";
    case GeothermalKind::MudPot:
        return "mud pot";
    case GeothermalKind::Fumarole:
        return "fumarole";
    }
    return "unknown";
}

} // namespace terrain

namespace terrain::volcano_water {

namespace vf = volcano_forms;
using volcano_relief::Detail;
using volcano_relief::Point;
using volcano_relief::Spring;
using volcano_relief::Zone;
using volcano_sites::between;
using volcano_sites::unit;

namespace {

enum Draw : std::uint64_t {
    kZoneTurn = 220,
    kZoneFrom,
    kZoneSize,
    kSpringCount,
    kSpring = 300, // + 8 per attempt
};

// The rim of the pool of a spring stands kRim meters above its water.
constexpr double kRim = 0.12;
// The springs keep within their zone; their terraces reach kZoneMargin meters beyond it at most.
constexpr double kZoneMargin = 120.0;
// The steepest ground a pool is dug in (meters per meter), a pot or a vent.
constexpr double kPoolSlope = 0.15;
constexpr double kVentSlope = 0.5;

// The shares of the hot springs, geysers and mud pots of a zone (the rest: fumaroles).
struct Mix {
    double hot = 0.0;
    double geyser = 0.0;
    double mud = 0.0;
};

// The zone of the springs of an active volcano and the mix of their kinds, false for none.
bool zoneOf(const VolcanoSite& s, Zone& z, Mix& mix) {
    const VolcanoOffset top = volcanoSummit(s);
    const auto crater = static_cast<double>(s.crater);
    double from = 0.0;
    double size = 0.0;
    switch (s.form) {
    case VolcanoForm::Caldera:
    case VolcanoForm::PlateauCaldera:
        // On its floor (Yellowstone, Taupo).
        from = between(s.seed, kZoneFrom, 0.1, 0.5) * crater;
        size = std::clamp(0.25 * crater, 300.0, 2500.0);
        mix = s.form == VolcanoForm::Caldera ? Mix{0.4, 0.15, 0.2} : Mix{0.45, 0.25, 0.15};
        break;
    case VolcanoForm::IslandCaldera:
        from = 1.25 * crater;
        size = std::clamp(0.15 * crater, 200.0, 800.0);
        mix = {0.3, 0.0, 0.2};
        break;
    case VolcanoForm::Strato:
    case VolcanoForm::TwinStrato:
    case VolcanoForm::CollapsedStrato:
    case VolcanoForm::IslandStrato:
        // By its crater: mostly fumaroles.
        from = 1.15 * crater + 100.0;
        size = between(s.seed, kZoneSize, 150.0, 400.0);
        mix = {0.1, 0.0, 0.2};
        break;
    case VolcanoForm::Shield:
    case VolcanoForm::IslandShield:
        from = 1.2 * crater;
        size = between(s.seed, kZoneSize, 300.0, 800.0);
        mix = {0.2, 0.0, 0.2};
        break;
    case VolcanoForm::LavaField:
    case VolcanoForm::FissureRow:
    case VolcanoForm::CinderField:
    case VolcanoForm::MaarField:
        // In the field (Iceland: Geysir, Haukadalur).
        from = between(s.seed, kZoneFrom, 0.0, 0.5) * static_cast<double>(s.radius);
        size = between(s.seed, kZoneSize, 400.0, 1500.0);
        mix = {0.45, 0.2, 0.2};
        break;
    default:
        return false;
    }
    const double turn = unit(s.seed, kZoneTurn);
    z.x = top.dx + from * dmath::cosTurns(turn);
    z.z = top.dz + from * dmath::sinTurns(turn);
    z.radius = size;
    return true;
}

// A spring drawn by its seed: its kind and sizes.
Spring drawSpring(std::uint64_t seed, std::uint64_t at, const Mix& mix) {
    Spring sp;
    const double u = unit(seed, at);
    const double v = unit(seed, at + 1);
    if (u < mix.hot) {
        sp.kind = GeothermalKind::HotSpring;
        sp.radius = 3.0 + 22.0 * v * v;
        sp.depth = between(seed, at + 2, 0.5, 2.0);
    } else if (u < mix.hot + mix.geyser) {
        sp.kind = GeothermalKind::Geyser;
        sp.radius = 1.5 + 2.5 * v;
        sp.depth = between(seed, at + 2, 1.5, 3.0);
        sp.mound = between(seed, at + 3, 0.4, 1.5);
    } else if (u < mix.hot + mix.geyser + mix.mud) {
        sp.kind = GeothermalKind::MudPot;
        sp.radius = 2.0 + 6.0 * v;
        sp.depth = between(seed, at + 2, 0.5, 1.2);
    } else {
        sp.kind = GeothermalKind::Fumarole;
        sp.radius = 3.0 + 7.0 * v;
    }
    return sp;
}

bool holdsWater(const Spring& sp) {
    return sp.kind == GeothermalKind::HotSpring || sp.kind == GeothermalKind::Geyser;
}

// How far the terrace of a spring reaches (radii of its pool), and where it stops being flat.
double outerOf(const Spring& sp) {
    return sp.kind == GeothermalKind::Geyser ? 4.0 : 2.2;
}

double flatOf(const Spring& sp) {
    return sp.kind == GeothermalKind::Geyser ? 1.6 : 1.3;
}

// The terrace of a spring at q radii of its pool from its center: its pool under the water, its
// rim, then its terrace or mound; continuous everywhere.
double terraceAt(const Spring& sp, double q) {
    if (q < 0.95) {
        return sp.level - sp.depth * (1.0 - vf::smoothstep(0.3, 0.95, q));
    }
    const double outer = outerOf(sp);
    const auto lift = [&](double at) {
        return kRim + (sp.kind == GeothermalKind::Geyser
                           ? sp.mound * (1.0 - vf::smoothstep(1.05, outer, at))
                           : 0.0);
    };
    if (q < 1.05) {
        return sp.level + lift(1.05) * vf::smoothstep(0.95, 1.05, q);
    }
    return sp.level + lift(q);
}

// How much of a spring a footprint keeps: all of it under a fifth of its radius, none from 0.6.
double keptAt(const Spring& sp, double footprint) {
    return 1.0 - vf::smoothstep(0.2 * sp.radius, 0.6 * sp.radius, footprint);
}

// Whether a spring at sp fits: in its volcano, apart from the others, out of its lakes, on dry
// gentle ground; its level from the ground there. real: the relief with the volcano.
template <typename Real>
bool fits(const VolcanoSite& s, const Detail& d, Spring& sp, const Real& real) {
    const double reach = outerOf(sp) * sp.radius;
    if (dmath::hypot(sp.x, sp.z) + reach > static_cast<double>(s.reach)) {
        return false;
    }
    for (const Spring& other : d.springs) {
        const double apart = 2.0 * (reach + outerOf(other) * other.radius) + 10.0;
        if (dmath::hypot(sp.x - other.x, sp.z - other.z) < apart) {
            return false;
        }
    }
    const double center = real(sp.x, sp.z);
    if (!(center > 1.0)) {
        return false;
    }
    for (const volcano_relief::Basin& b : d.basins) {
        if (within(b, sp.x, sp.z) && center < b.spill) {
            return false;
        }
    }
    const double step = 2.0 * sp.radius;
    const double a = real(sp.x + step, sp.z);
    const double b = real(sp.x - step, sp.z);
    const double c = real(sp.x, sp.z + step);
    const double e = real(sp.x, sp.z - step);
    const double rise = std::max({a, b, c, e, center}) - std::min({a, b, c, e, center});
    if (rise > 2.0 * step * (holdsWater(sp) ? kPoolSlope : kVentSlope)) {
        return false;
    }
    sp.level = center - 0.05;
    return true;
}

} // namespace

void addSprings(const VolcanoSite& s, Detail& d, const volcano_relief::GroundAt& ground) {
    Mix mix;
    Zone zone;
    if (!s.active || !zoneOf(s, zone, mix)) {
        return;
    }
    const auto real = [&](double dx, double dz) {
        const volcano_relief::Ground g = ground(s.x + dx, s.z + dz);
        Point p;
        p.height = g.height;
        p.regional = g.regional;
        volcano_relief::apply(s, d, dx, dz, 0.0, p);
        return p.height;
    };
    const int count = 5 + static_cast<int>(20.0 * unit(s.seed, kSpringCount));
    for (int attempt = 0; attempt < 4 * count && static_cast<int>(d.springs.size()) < count;
         ++attempt) {
        const std::uint64_t at = kSpring + 8 * static_cast<std::uint64_t>(attempt);
        Spring sp = drawSpring(s.seed, at, mix);
        const double r = zone.radius * std::sqrt(unit(s.seed, at + 4));
        const double turn = unit(s.seed, at + 5);
        sp.x = zone.x + r * dmath::cosTurns(turn);
        sp.z = zone.z + r * dmath::sinTurns(turn);
        if (fits(s, d, sp, real)) {
            d.springs.push_back(sp);
        }
    }
    if (!d.springs.empty()) {
        d.zone = zone;
    }
}

void applySprings(const Detail& d, double dx, double dz, double footprint, Point& p) {
    if (d.springs.empty()) {
        return;
    }
    const double zx = dx - d.zone.x;
    const double zz = dz - d.zone.z;
    const double margin = d.zone.radius + kZoneMargin;
    if (!(zx * zx + zz * zz < margin * margin)) {
        return;
    }
    for (const Spring& sp : d.springs) {
        const double ox = dx - sp.x;
        const double oz = dz - sp.z;
        const double reach = outerOf(sp) * sp.radius;
        const double r2 = ox * ox + oz * oz;
        const double kept = keptAt(sp, footprint);
        if (!(r2 < reach * reach) || !(kept > 0.0)) {
            continue;
        }
        const double q = std::sqrt(r2) / sp.radius;
        if (sp.kind == GeothermalKind::MudPot) {
            p.height -= kept * sp.depth * (1.0 - vf::smoothstep(0.2, 1.0, q));
        } else if (holdsWater(sp)) {
            const double weight = 1.0 - vf::smoothstep(flatOf(sp), outerOf(sp), q);
            p.height += kept * weight * (terraceAt(sp, q) - p.height);
        } else {
            continue;
        }
        p.cover = std::max(p.cover, static_cast<float>(kept));
        p.touched = true;
    }
}

const Spring* poolAt(const Detail& d, double dx, double dz, double footprint) {
    if (d.springs.empty()) {
        return nullptr;
    }
    const double zx = dx - d.zone.x;
    const double zz = dz - d.zone.z;
    const double margin = d.zone.radius + kZoneMargin;
    if (!(zx * zx + zz * zz < margin * margin)) {
        return nullptr;
    }
    for (const Spring& sp : d.springs) {
        const double ox = dx - sp.x;
        const double oz = dz - sp.z;
        const double inner = 0.95 * sp.radius;
        if (holdsWater(sp) && ox * ox + oz * oz < inner * inner && !(keptAt(sp, footprint) < 1.0)) {
            return &sp;
        }
    }
    return nullptr;
}

} // namespace terrain::volcano_water
