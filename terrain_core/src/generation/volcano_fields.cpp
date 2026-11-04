#include "volcano_fields.hpp"

#include "terrain/det_math.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain::volcano_fields {

namespace vf = volcano_forms;
using volcano_relief::Cone;
using volcano_relief::Detail;
using volcano_relief::Flow;
using volcano_relief::Point;

namespace {

enum Stream : std::uint64_t {
    kSheetOutline = 201,
    kSheetLobes,
    kSheetFine,
    kSheetCrust,
    kSheetSwell,
};

// The crust of the lava: rough over a few tens of meters, gone where the footprint is coarser.
double crust(std::uint64_t seed, double dx, double dz, double footprint, double amplitude) {
    const double keep = 1.0 - vf::smoothstep(3.5, 10.0, footprint);
    if (!(keep > 0.0)) {
        return 0.0;
    }
    return amplitude * keep * vf::planeNoise(seed, dx / 35.0, dz / 35.0);
}

double coneAt(const Cone& c, double ox, double oz, double r) {
    double h = vf::cinderCone(r, c.height, c.radius, c.crater, c.depth);
    if (c.breached && r > 0.0) {
        // A horseshoe: its side toward the breach lowered to the floor of its crater.
        const double across = vf::angleBetween(dmath::atan2(oz, ox), c.breachAngle) / 0.5;
        if (std::abs(across) < 1.0) {
            const double open = (1.0 - std::abs(across)) * (1.0 - std::abs(across));
            const double low = (c.height - c.depth) * (1.0 - vf::smoothstep(c.crater, c.radius, r));
            h -= open * std::max(0.0, h - low);
        }
    }
    return h;
}

} // namespace

void applySheet(const VolcanoSite& s, const Detail& d, double dx, double dz, double footprint,
                Point& p) {
    if (s.form != VolcanoForm::LavaField && s.form != VolcanoForm::FissureRow) {
        return;
    }
    const volcano_relief::Shape& sh = d.shape;
    const double radius = static_cast<double>(s.radius);
    const double thick = static_cast<double>(s.height);
    const double u = (dx * sh.cosAxis + dz * sh.sinAxis) / static_cast<double>(s.elongation);
    const double v = -dx * sh.sinAxis + dz * sh.cosAxis;
    const double r = dmath::hypot(u, v);
    if (!(r < radius * (1.0 + volcano_sites::kFieldLobes) * (1.0 + volcano_sites::kFieldOutline))) {
        return;
    }
    double turns = r > 0.0 ? dmath::atan2(v, u) / vf::kTwoPi : 0.0;
    turns -= std::floor(turns);
    const double outline =
        radius * (1.0 + volcano_sites::kFieldOutline *
                            vf::ringNoise(s.seed + kSheetOutline, turns, 9));
    const double raw = 1.0 - r / outline +
                       0.3 * vf::planeNoise(s.seed + kSheetLobes, dx / 4000.0, dz / 4000.0) +
                       0.12 * vf::planeNoise(s.seed + kSheetFine, dx / 900.0, dz / 900.0);
    const double band = std::clamp(3.0 * thick * (1.0 / radius + 2.5e-4), 0.004, 0.05);
    const double mask = vf::smoothstep(0.0, band, raw);
    if (!(mask > 0.0)) {
        return;
    }
    const double level = p.regional + d.sheetOffset + thick;
    // Its pressure ridges and tumuli over hundreds of meters, its lobes over kilometers.
    const double swell = 3.0 * vf::planeNoise(s.seed + kSheetSwell, dx / 250.0, dz / 250.0) +
                         6.0 * vf::planeNoise(s.seed + kSheetSwell + 1, dx / 1500.0, dz / 1500.0);
    const double top = vf::smax(p.height, level, 6.0) + swell +
                       crust(s.seed + kSheetCrust, dx, dz, footprint, 2.0);
    p.height += mask * (top - p.height);
    p.cover = std::max(p.cover, static_cast<float>(mask));
    p.touched = true;
}

void applyFlows(const VolcanoSite& s, const Detail& d, double dx, double dz, double footprint,
                Point& p) {
    (void)s;
    for (const Flow& f : d.flows) {
        const double ox = dx - f.x;
        const double oz = dz - f.z;
        const double along = ox * f.dirX + oz * f.dirZ;
        if (!(along > -f.width) || !(along < f.length + f.width)) {
            continue;
        }
        const double lateral = -ox * f.dirZ + oz * f.dirX;
        if (!(std::abs(lateral) < f.meander + 1.4 * f.width)) {
            continue;
        }
        // It wanders over kilometers, widens downslope from a narrow channel to its lobes, its
        // edges lobed over hundreds of meters.
        const double wander = f.meander * (vf::lineNoise(f.seed, along / 1800.0) +
                                           0.4 * vf::lineNoise(f.seed + 3, along / 600.0));
        const double off = std::abs(lateral - wander);
        const double grow = vf::smoothstep(0.0, f.length, along);
        const double width = f.width * (0.35 + 0.65 * grow) *
                             (1.0 + 0.3 * vf::lineNoise(f.seed + 4, along / 300.0));
        if (!(off < width)) {
            continue;
        }
        const double edge = std::clamp(2.0 * f.thickness, 8.0, 0.35 * width);
        const double lobe = 0.5 + 0.5 * vf::lineNoise(f.seed + 1, lateral / width * 1.5);
        const double front = f.length - 0.3 * width * lobe;
        const double mask = (1.0 - vf::smoothstep(width - edge, width, off)) *
                            vf::smoothstep(-edge, 0.0, along) *
                            (1.0 - vf::smoothstep(front - 2.5 * f.thickness, front, along));
        if (!(mask > 0.0)) {
            continue;
        }
        // Thin where it ran in its channel, thick in its lobes; its levees low.
        const double thick = f.thickness * (0.5 + 0.5 * grow);
        const double levee = 0.12 * thick * vf::smoothstep(width - 3.0 * edge, width - edge, off);
        p.height += mask * (thick + levee + crust(f.seed + 2, dx, dz, footprint, 2.0));
        p.cover = std::max(p.cover, static_cast<float>(mask));
        p.touched = true;
    }
}

void applyCones(const VolcanoSite& s, const Detail& d, double dx, double dz, Point& p) {
    (void)s;
    for (const Cone& c : d.cones) {
        const double ox = dx - c.x;
        const double oz = dz - c.z;
        const double r2 = ox * ox + oz * oz;
        if (!(r2 < c.reach * c.reach)) {
            continue;
        }
        const double r = std::sqrt(r2);
        if (c.maar) {
            p.height += vf::maar(r, c.height, c.radius, c.ring);
            p.cover = std::max(p.cover, static_cast<float>(1.0 - vf::smoothstep(c.radius,
                                                                                  2.2 * c.radius,
                                                                                  r)));
        } else {
            p.height += coneAt(c, ox, oz, r);
            p.cover = std::max(p.cover,
                               static_cast<float>(1.0 - vf::smoothstep(0.7 * c.radius, c.radius, r)));
        }
        p.touched = true;
    }
}

} // namespace terrain::volcano_fields
