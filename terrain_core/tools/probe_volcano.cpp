// The section volcano of terrain_probe (probe_sections.hpp, agent 45): the volcano whose reach holds
// the point (the nearest relative to its reach), its form, its origin in the plate tectonics, its
// sizes, where its summit lies and how far, how much of the ground there is its work, the ground
// it left there and how bare; the lake of the crater over the point, the geothermal site nearest
// it.

#include "probe_sections.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/volcanoes.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace probe {

namespace {

// The volcano whose reach holds (x, z), nearest relative to its reach, or none.
const terrain::VolcanoSite* volcanoAt(const terrain::Heightfield& field, double x, double z) {
    const terrain::Volcanoes* v = field.volcanoes();
    if (v == nullptr) {
        return nullptr;
    }
    const terrain::VolcanoSite* best = nullptr;
    double nearest = std::numeric_limits<double>::infinity();
    for (const std::uint32_t i : v->near(x, z)) {
        const terrain::VolcanoSite& s = v->sites()[i];
        const terrain::VolcanoOffset o = terrain::volcanoOffset(field.world(), s.x, s.z, x, z);
        const double share = terrain::dmath::hypot(o.dx, o.dz) / static_cast<double>(s.reach);
        if (share < 1.0 && share < nearest) {
            nearest = share;
            best = &s;
        }
    }
    return best;
}

// The lake of the crater, caldera or maar over the point (its level, with the planet level) and
// the geothermal site nearest the point within kSpringSearch meters (its kind).
constexpr double kSpringSearch = 100.0;

void lakeAndSpring(const ProbeWorld& w, const Point& p, const terrain::VolcanoSite* s, Record& r) {
    if (s == nullptr) {
        r.none("lakeLevel").none("geothermal");
        return;
    }
    const terrain::Heightfield& field = w.field();
    const double reach = static_cast<double>(s->reach);
    double best = std::numeric_limits<double>::infinity();
    float level = std::numeric_limits<float>::quiet_NaN();
    for (const terrain::VolcanoBasin& b :
         field.volcanoBasinsIn(p.x - reach, p.z - reach, p.x + reach, p.z + reach)) {
        const terrain::VolcanoOffset o = terrain::volcanoOffset(w.world(), b.x, b.z, p.x, p.z);
        const double d = terrain::dmath::hypot(o.dx, o.dz) / static_cast<double>(b.radius);
        if (d < 1.0 && d < best && p.sample.water != terrain::WaterKind::None &&
            p.sample.waterLevel == b.level) {
            best = d;
            level = b.level;
        }
    }
    if (std::isnan(level)) {
        r.none("lakeLevel");
    } else {
        r.number("lakeLevel", static_cast<double>(level), 2, "m");
    }
    const terrain::GeothermalSite* spring = nullptr;
    best = kSpringSearch;
    const std::vector<terrain::GeothermalSite> springs =
        field.geothermalIn(p.x - kSpringSearch, p.z - kSpringSearch, p.x + kSpringSearch,
                           p.z + kSpringSearch);
    for (const terrain::GeothermalSite& g : springs) {
        const double d = terrain::dmath::hypot(g.x - p.x, g.z - p.z);
        if (d < best) {
            best = d;
            spring = &g;
        }
    }
    if (spring == nullptr) {
        r.none("geothermal");
    } else {
        r.text("geothermal", terrain::geothermalKindName(spring->kind));
    }
}

} // namespace

Record volcanoSection(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::VolcanoSite* s = volcanoAt(w.relief(), p.x, p.z);
    r.number("cover", static_cast<double>(p.sample.volcanic) / 255.0, 3);
    if (s == nullptr) {
        for (const char* key : {"form", "origin", "active", "large", "atSea", "centerX", "centerZ",
                                "summitX", "summitZ", "distance", "heading", "height", "radius",
                                "crater", "craterDepth", "reach"}) {
            r.none(key);
        }
    } else {
        const terrain::VolcanoOffset top = terrain::volcanoSummit(*s);
        const double sx = terrain::wrapCoordinate(s->x + top.dx, w.world().x);
        const double sz = terrain::wrapCoordinate(s->z + top.dz, w.world().z);
        const terrain::VolcanoOffset o = terrain::volcanoOffset(w.world(), p.x, p.z, sx, sz);
        const double distance = terrain::dmath::hypot(o.dx, o.dz);
        r.text("form", terrain::volcanoFormName(s->form))
            .text("origin", terrain::volcanoOriginName(s->origin))
            .flag("active", s->active)
            .flag("large", s->large)
            .flag("atSea", s->atSea)
            .number("centerX", s->x, 1, "m")
            .number("centerZ", s->z, 1, "m")
            .number("summitX", sx, 1, "m")
            .number("summitZ", sz, 1, "m")
            .number("distance", distance, 1, "m");
        if (distance > 0.0) {
            r.number("heading", headingOf(o.dx, o.dz), 1, "deg");
        } else {
            r.none("heading");
        }
        r.number("height", static_cast<double>(s->height), 1, "m")
            .number("radius", static_cast<double>(s->radius), 1, "m")
            .number("crater", static_cast<double>(s->crater), 1, "m")
            .number("craterDepth", static_cast<double>(s->craterDepth), 1, "m")
            .number("reach", static_cast<double>(s->reach), 1, "m");
    }
    // The ground they left there (VolcanicGround) and how bare it lies.
    if (p.sample.volcanicShare > 0) {
        r.text("ground", terrain::volcanicGroundName(p.sample.volcanicGround))
            .number("groundShare", static_cast<double>(p.sample.volcanicShare) / 255.0, 3);
    } else {
        r.none("ground").none("groundShare");
    }
    lakeAndSpring(w, p, s, r);
    return r;
}

} // namespace probe
