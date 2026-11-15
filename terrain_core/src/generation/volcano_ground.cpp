// The ground the volcanoes leave (volcano_ground.hpp).
#include "volcano_ground.hpp"

#include "terrain/det_math.hpp"
#include "volcano_forms.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

const char* volcanicGroundName(VolcanicGround ground) {
    switch (ground) {
    case VolcanicGround::None:
        return "none";
    case VolcanicGround::Basalt:
        return "basalt";
    case VolcanicGround::Ash:
        return "ash";
    case VolcanicGround::Scoria:
        return "scoria";
    case VolcanicGround::CooledLava:
        return "cooled lava";
    case VolcanicGround::Geothermal:
        return "geothermal";
    }
    return "unknown";
}

} // namespace terrain

namespace terrain::volcano_ground {

namespace vf = volcano_forms;
using volcano_relief::Point;

namespace {

// How bare the ground of an extinct volcano lies against that of an active one: plants and soil
// grow over it.
constexpr double kWeathered = 0.6;

double ageOf(const VolcanoSite& s) {
    return s.active ? 1.0 : kWeathered;
}

bool isStrato(VolcanoForm f) {
    return f == VolcanoForm::Strato || f == VolcanoForm::TwinStrato ||
           f == VolcanoForm::CollapsedStrato || f == VolcanoForm::IslandStrato;
}

bool isShield(VolcanoForm f) {
    return f == VolcanoForm::Shield || f == VolcanoForm::IslandShield ||
           f == VolcanoForm::Seamount;
}

} // namespace

void offer(Point& p, VolcanicGround ground, double share) {
    const auto s = static_cast<float>(std::clamp(share, 0.0, 1.0));
    if (s > p.groundShare) {
        p.groundShare = s;
        p.ground = ground;
    }
}

void edifice(const VolcanoSite& s, double rho, double r, double dominance, Point& p) {
    if (!(dominance > 0.0)) {
        return;
    }
    const auto crater = static_cast<double>(s.crater);
    const double q = crater > 0.0 ? r / crater : 1e9;
    if (isStrato(s.form)) {
        // Ash down the upper slopes, scoria around the crater of an active cone.
        offer(p, VolcanicGround::Ash,
              dominance * (1.0 - vf::smoothstep(0.2, 0.55, rho)) * (s.active ? 0.9 : kWeathered));
        if (q < 1.6) {
            offer(p, s.active ? VolcanicGround::Scoria : VolcanicGround::Ash,
                  dominance * (1.0 - vf::smoothstep(1.2, 1.6, q)) * (s.active ? 1.0 : 0.8));
        }
    } else if (isShield(s.form)) {
        offer(p, VolcanicGround::Basalt,
              dominance * (1.0 - vf::smoothstep(0.25, 0.85, rho)) * (s.active ? 0.9 : 0.55));
        if (q < 1.2) {
            offer(p, s.active ? VolcanicGround::CooledLava : VolcanicGround::Basalt,
                  dominance * (1.0 - vf::smoothstep(1.0, 1.2, q)) * (s.active ? 1.0 : 0.8));
        }
    } else if (s.form == VolcanoForm::PlateauCaldera) {
        // A plateau of ash (ignimbrite), its walls of tuff.
        offer(p, VolcanicGround::Ash, dominance * 0.45 * (1.0 - vf::smoothstep(0.6, 1.0, rho)));
        offer(p, VolcanicGround::Ash, dominance * 0.65 * (1.0 - vf::smoothstep(0.1, 0.2,
                                                                                std::abs(q - 0.9))));
    } else {
        // A caldera: basalt down its walls, ash on its floor and its flanks.
        offer(p, VolcanicGround::Ash,
              dominance * 0.5 * (1.0 - vf::smoothstep(0.3, 0.8, rho)));
        offer(p, VolcanicGround::Ash, dominance * 0.6 * (1.0 - vf::smoothstep(0.75, 0.85, q)));
        offer(p, VolcanicGround::Basalt,
              dominance * 0.85 * (1.0 - vf::smoothstep(0.1, 0.2, std::abs(q - 0.92))));
    }
}

void sheet(const VolcanoSite& s, double cover, Point& p) {
    offer(p, s.active ? VolcanicGround::CooledLava : VolcanicGround::Basalt,
          cover * (s.active ? 0.95 : 0.55));
}

void flow(const VolcanoSite& s, double mask, Point& p) {
    offer(p, s.active ? VolcanicGround::CooledLava : VolcanicGround::Basalt, mask * ageOf(s));
}

void cone(const VolcanoSite& s, bool maar, double cover, Point& p) {
    if (maar) {
        offer(p, VolcanicGround::Ash, 0.5 * cover);
    } else {
        offer(p, VolcanicGround::Scoria, cover * (s.active ? 1.0 : 0.75));
    }
}

void springs(const volcano_relief::Detail& d, double dx, double dz, Point& p) {
    if (d.springs.empty()) {
        return;
    }
    // Over the zone, altered ground; around each spring, its sinter all bare: it wins over any
    // other ground as bare.
    const double r = dmath::hypot(dx - d.zone.x, dz - d.zone.z);
    double share = 0.8 * (1.0 - vf::smoothstep(0.6 * d.zone.radius, d.zone.radius + 60.0, r));
    if (r < d.zone.radius + 200.0) {
        for (const volcano_relief::Spring& sp : d.springs) {
            const double near = std::max(10.0, 2.0 * sp.radius);
            share = std::max(share, 1.0 - vf::smoothstep(near, 2.0 * near,
                                                         dmath::hypot(dx - sp.x, dz - sp.z)));
        }
    }
    const auto bare = static_cast<float>(std::clamp(share, 0.0, 1.0));
    if (bare > 0.0f && !(bare < p.groundShare)) {
        p.groundShare = bare;
        p.ground = VolcanicGround::Geothermal;
    }
}

} // namespace terrain::volcano_ground
