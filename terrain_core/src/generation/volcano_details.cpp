// The details of a volcano (volcano_relief.hpp): its shape from its seed, then, read once from the
// relief without the volcanoes, the ground it stands on, its cones and maars (volcano_cones.cpp)
// and its flows, each cut where the ground ahead rises above it.
#include "volcano_relief.hpp"

#include "terrain/det_math.hpp"
#include "volcano_cones.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"
#include "volcano_water.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain::volcano_relief {

namespace vf = volcano_forms;
using volcano_sites::between;
using volcano_sites::unit;

namespace {

// The indices of the draws of a shape (those of volcano_draws.cpp stay below 16).
enum Draw : std::uint64_t {
    kLobe1 = 16,
    kPhase1,
    kLobe2,
    kPhase2,
    kRagged,
    kCells,
    kOffset,
    kOffsetAngle,
    kSharpnessUnused, // drawn with the sizes since agent 45's stop 1 (VolcanoSite::sharpness)
    kBreached,
    kBreachAngle,
    kBreachWidth,
    kBreachBottom,
    kWall,
    kCollapseAngle,
    kRavineSpacing,
    kRavineDepth,
    kFlowCount,
    kBaseTurn,
    kArms,
    kArmTurn,
    kArmReach = kArmTurn + 4,
    kArmWidth = kArmReach + 3,
};

bool isCaldera(VolcanoForm f) {
    return f == VolcanoForm::Caldera || f == VolcanoForm::PlateauCaldera ||
           f == VolcanoForm::IslandCaldera;
}

// The sharpness of the profile of a cone (drawn with its sizes), 0 for a dome.
double sharpnessOf(const VolcanoSite& s) {
    return static_cast<double>(s.sharpness);
}

// The share of the radius of the crater its wall takes, from the mean slope of the wall: about 35
// degrees for a crater, 31 to 50 for a caldera (Crater Lake: 35 to 40), 50 to 68 for the pit of
// a shield.
double wallOf(const VolcanoSite& s) {
    double slope = 0.7;
    if (s.form == VolcanoForm::Caldera || s.form == VolcanoForm::IslandCaldera) {
        slope = between(s.seed, kWall, 0.6, 1.2);
    } else if (s.form == VolcanoForm::PlateauCaldera) {
        slope = between(s.seed, kWall, 0.5, 1.0);
    } else if (volcano_sites::formRule(s.form).shield) {
        slope = between(s.seed, kWall, 1.2, 2.5);
    }
    const double crater = std::max(1.0, static_cast<double>(s.crater));
    return std::clamp(static_cast<double>(s.craterDepth) / (slope * crater), 0.05, 0.95);
}

void ravinesOf(const VolcanoSite& s, Shape& sh) {
    const volcano_sites::FormRule rule = volcano_sites::formRule(s.form);
    if (!rule.edifice || s.form == VolcanoForm::Seamount) {
        return;
    }
    const double spacing = between(s.seed, kRavineSpacing, 1500.0, 3500.0);
    sh.ravines = static_cast<int>(
        std::clamp(std::floor(vf::kPi * static_cast<double>(s.radius) / spacing + 0.5), 8.0,
                   300.0));
    double depth = s.active ? between(s.seed, kRavineDepth, 15.0, 50.0)
                            : between(s.seed, kRavineDepth, 40.0, 150.0);
    if (s.form == VolcanoForm::PlateauCaldera) {
        // The valleys cut into its plateau of ash.
        depth = between(s.seed, kRavineDepth, 20.0, 60.0);
    } else if (rule.shield) {
        depth *= 0.35;
    }
    if (s.atSea) {
        depth *= 0.8;
    }
    sh.ravineDepth = depth;
}

} // namespace

Shape shapeOf(const VolcanoSite& s) {
    Shape sh;
    sh.cosAxis = dmath::cos(static_cast<double>(s.axis));
    sh.sinAxis = dmath::sin(static_cast<double>(s.axis));
    sh.lobe1 = between(s.seed, kLobe1, 0.02, 0.07);
    sh.phase1 = unit(s.seed, kPhase1);
    sh.lobe2 = between(s.seed, kLobe2, 0.01, 0.04);
    sh.phase2 = unit(s.seed, kPhase2);
    sh.ragged = between(s.seed, kRagged, 0.01, 0.04);
    sh.cells = 7 + static_cast<int>(7.0 * unit(s.seed, kCells));
    if (volcano_sites::formRule(s.form).edifice) {
        const double offset = between(s.seed, kOffset, 0.03, 0.12) * static_cast<double>(s.radius);
        const double angle = vf::kTwoPi * unit(s.seed, kOffsetAngle);
        sh.offsetU = offset * dmath::cos(angle);
        sh.offsetV = offset * dmath::sin(angle);
    }
    if (volcano_sites::formRule(s.form).shield && s.form != VolcanoForm::PlateauCaldera) {
        // Two or three rift zones (Hawaii: ridges stretching the island tens of kilometers).
        sh.arms = 2 + static_cast<int>(2.0 * unit(s.seed, kArms));
        const double first = unit(s.seed, kArmTurn);
        for (std::size_t k = 0; k < static_cast<std::size_t>(sh.arms); ++k) {
            sh.armTurns[k] = first + static_cast<double>(k) / sh.arms +
                             0.12 * (unit(s.seed, kArmTurn + 1 + k) - 0.5);
            sh.armReach[k] = between(s.seed, kArmReach + k, 0.25, volcano_sites::kArms);
        }
        sh.armWidth = between(s.seed, kArmWidth, 0.04, 0.08);
    }
    sh.sharpness = sharpnessOf(s);
    const double rise = s.atSea ? static_cast<double>(s.height) - static_cast<double>(s.floor)
                                : static_cast<double>(s.height);
    sh.smoothing = std::clamp(0.012 * rise, 15.0, 80.0);
    sh.swell = (s.form == VolcanoForm::PlateauCaldera
                    ? 0.08
                    : (volcano_sites::formRule(s.form).shield ? 0.025 : 0.012)) *
               rise;
    sh.rimNoise = isCaldera(s.form) ? 0.06 : 0.08;
    const double breachChance = s.form == VolcanoForm::IslandCaldera
                                    ? 0.7
                                    : (s.form == VolcanoForm::Caldera
                                           ? 0.5
                                           : (s.form == VolcanoForm::PlateauCaldera ? 0.4 : 0.0));
    sh.breached = unit(s.seed, kBreached) < breachChance;
    sh.breachAngle = vf::kTwoPi * unit(s.seed, kBreachAngle);
    // A notch a few hundred meters wide in the rim (at sea, wider: the strait into a drowned
    // caldera).
    sh.breachWidth = between(s.seed, kBreachWidth, 0.05, s.form == VolcanoForm::IslandCaldera
                                                              ? 0.25
                                                              : 0.1);
    sh.wall = wallOf(s);
    sh.collapseAngle = vf::kTwoPi * unit(s.seed, kCollapseAngle);
    ravinesOf(s, sh);
    return sh;
}

} // namespace terrain::volcano_relief

namespace terrain {

VolcanoOffset volcanoSummit(const VolcanoSite& site) {
    const volcano_relief::Shape sh = volcano_relief::shapeOf(site);
    const double u = sh.offsetU * static_cast<double>(site.elongation);
    return {u * sh.cosAxis - sh.offsetV * sh.sinAxis, u * sh.sinAxis + sh.offsetV * sh.cosAxis};
}

} // namespace terrain

namespace terrain::volcano_relief {

namespace {

// The offset from the center of a point at rho (in radii of the foot) and turns in the frame of
// the volcano (its long axis stretched back).
struct Offset {
    double dx = 0.0;
    double dz = 0.0;
};

Offset offsetAt(const VolcanoSite& s, const Shape& sh, double rho, double turns) {
    const double r = rho * static_cast<double>(s.radius);
    const double u = r * dmath::cosTurns(turns) * static_cast<double>(s.elongation);
    const double v = r * dmath::sinTurns(turns);
    return {u * sh.cosAxis - v * sh.sinAxis, u * sh.sinAxis + v * sh.cosAxis};
}

// The ground under a volcano on land. An edifice stands on the low ground at its foot (the lower
// quarter of rings at 0.7 and 1 of its foot: it rises from the valleys around, never fills them
// to the mean of the hills), and at least half its height under the highest ground of its heart
// (within 0.3 of its foot, and within the rim of a caldera): a cone among mountains towers over
// them, a caldera is never dug into them. A field lies on the mean of its ground.
double groundUnder(const VolcanoSite& s, const Shape& sh, const GroundAt& ground) {
    const double turn = unit(s.seed, kBaseTurn);
    const auto ring = [&](double rho, int count, std::vector<double>& out) {
        for (int k = 0; k < count; ++k) {
            const Offset o = offsetAt(s, sh, rho, turn + (k + 0.5 * rho) / count);
            out.push_back(ground(s.x + o.dx, s.z + o.dz).height);
        }
    };
    std::vector<double> foot;
    ring(0.7, 16, foot);
    ring(1.0, 16, foot);
    if (!volcano_sites::formRule(s.form).edifice) {
        ring(0.35, 8, foot);
        foot.push_back(ground(s.x, s.z).height);
        double sum = 0.0;
        for (const double h : foot) {
            sum += h;
        }
        return sum / static_cast<double>(foot.size());
    }
    std::sort(foot.begin(), foot.end());
    const double low = foot[foot.size() / 4];
    std::vector<double> heart{ground(s.x, s.z).height};
    ring(0.15, 8, heart);
    ring(0.3, 8, heart);
    const double rim = static_cast<double>(s.crater) / static_cast<double>(s.radius);
    if (rim > 0.3) {
        ring(0.5 * rim, 12, heart);
        ring(rim, 12, heart);
    }
    const double high = *std::max_element(heart.begin(), heart.end());
    return std::max(low, high - 0.5 * static_cast<double>(s.height));
}

// The bottom of the breach of a crater: on land, part way up its wall (where a lake spills); at
// sea, under the sea (the sea fills a drowned caldera).
double breachBottomOf(const VolcanoSite& s, const Detail& d) {
    if (s.form == VolcanoForm::IslandCaldera) {
        return -between(s.seed, kBreachBottom, 20.0, 80.0);
    }
    const double rimRho = static_cast<double>(s.crater) / static_cast<double>(s.radius);
    const double rim = edificeBase(s, d) + edificeRise(s, d.shape) * profileOf(s, d.shape, rimRho);
    const double floor = rim - static_cast<double>(s.craterDepth);
    return floor + (rim - floor) * between(s.seed, kBreachBottom, 0.35, 0.85);
}

// Cuts a flow where the surface ahead of it rises above what it can climb, and where it would
// leave the reach of its volcano.
void cutFlow(const VolcanoSite& s, const Detail& d, const GroundAt& ground, Flow& f) {
    const double step = std::max(150.0, f.length / 24.0);
    double previous = 0.0;
    bool first = true;
    for (double along = 0.5 * f.length; along <= f.length; along += step) {
        const double dx = f.x + f.dirX * along;
        const double dz = f.z + f.dirZ * along;
        if (dmath::hypot(dx, dz) + f.width > static_cast<double>(s.reach)) {
            f.length = std::max(0.0, along - step);
            return;
        }
        const double here = surfaceBeforeFlows(s, d, dx, dz, ground(s.x + dx, s.z + dz));
        if (!first && here > previous + 0.5 * f.thickness) {
            f.length = along - step;
            return;
        }
        previous = here;
        first = false;
    }
}

} // namespace

Detail detailOf(const VolcanoSite& s, const GroundAt& ground) {
    Detail d;
    d.shape = shapeOf(s);
    if (s.atSea) {
        d.base = static_cast<double>(s.floor);
    } else {
        d.base = groundUnder(s, d.shape, ground);
        d.sheetOffset = d.base - ground(s.x, s.z).regional;
    }
    if (d.shape.breached) {
        d.shape.breachBottom = breachBottomOf(s, d);
    }
    volcano_cones::addCones(s, d);
    volcano_cones::addFlows(s, d);
    for (Flow& f : d.flows) {
        cutFlow(s, d, ground, f);
    }
    std::erase_if(d.flows, [](const Flow& f) { return !(f.length > 2.0 * f.width); });
    volcano_water::addBasins(s, d, ground);
    volcano_water::addSprings(s, d, ground);
    return d;
}

} // namespace terrain::volcano_relief
