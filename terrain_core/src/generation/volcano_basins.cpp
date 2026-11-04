// The basins of the volcanoes and their lakes (volcano_water.hpp). The rim of a crater or caldera
// is read once on rays from its center over the relief of the volcano alone (over a ground far
// below it, which never raises a rim: the real ground only lifts the edifice and spares its
// crater), that of a maar over the real ground around it. A lake stands at a single level under
// the lowest point of the rim (or the bottom of the breach), the fuller the wetter its climate,
// and only within the inner wall that rises to that point: never above the rim, never spilling.
#include "volcano_water.hpp"

#include "terrain/det_math.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace terrain::volcano_water {

namespace vf = volcano_forms;
using volcano_relief::Basin;
using volcano_relief::Detail;
using volcano_relief::Point;
using volcano_sites::unit;

namespace {

// The draws of the water of a volcano (apart from those of its shape and its cones).
enum Draw : std::uint64_t {
    kLake = 200,
    kMaarLake = 201, // + the index of the maar among the cones
};

constexpr double kInf = std::numeric_limits<double>::infinity();
// The rays: one every kSpacing meters of the rim (kLeastRays to kMostRays), kSteps samples out to
// kOuter radii of the crater.
constexpr double kSpacing = 50.0;
constexpr int kLeastRays = 48;
constexpr int kMostRays = 256;
constexpr int kSteps = 32;
constexpr double kOuter = 1.45;
constexpr int kMaarRays = 48;
// How far below the edifice the ground of the rays lies.
constexpr double kBelow = 5000.0;
// The humidity of the climate (PlanetLevel::humidityAt) from which a basin holds water, at which
// it is full to its spill, under which its lake is salty; the least depth of a lake.
constexpr double kDry = 0.2;
constexpr double kWet = 0.5;
constexpr double kSalt = 0.32;
constexpr double kLeast = 1.0;

// The chance that a crater or caldera holds a lake under a wet climate: most calderas (Crater
// Lake, Toba), a quarter of the craters of the extinct cones, few of the active ones and of the
// shields, none on an amphitheater open down a flank, nor in a caldera the sea floods.
double lakeChance(const VolcanoSite& s, const Detail& d) {
    switch (s.form) {
    case VolcanoForm::Caldera:
    case VolcanoForm::PlateauCaldera:
        return 0.85;
    case VolcanoForm::IslandCaldera:
        return d.shape.breached && d.shape.breachBottom < 0.0 ? 0.0 : 0.7;
    case VolcanoForm::Strato:
    case VolcanoForm::TwinStrato:
    case VolcanoForm::IslandStrato:
        return s.active ? 0.05 : 0.25;
    case VolcanoForm::Shield:
    case VolcanoForm::IslandShield:
        return 0.05;
    default:
        return 0.0;
    }
}

// The relief of the volcano alone at (dx, dz) from its center, over a ground far below it.
double alone(const VolcanoSite& s, const Detail& d, double dx, double dz) {
    Point p;
    p.height = volcano_relief::edificeBase(s, d) - kBelow;
    p.regional = p.height;
    volcano_relief::apply(s, d, dx, dz, 0.0, p);
    return p.height;
}

// The margin of a lake under the lowest point of its rim (5 % of the depth, 4 m at least).
double freeboardOf(double depth) {
    return std::max(4.0, 0.05 * depth);
}

// The rays of a crater: one every kSpacing meters of its rim, two at least across its breach.
int raysOf(const VolcanoSite& s, const Detail& d) {
    double n = vf::kTwoPi * static_cast<double>(s.crater) / kSpacing;
    if (d.shape.breached) {
        n = std::max(n, 2.0 * vf::kTwoPi * static_cast<double>(s.elongation) /
                            d.shape.breachWidth);
    }
    return std::clamp(static_cast<int>(std::ceil(n)), kLeastRays, kMostRays);
}

// The basin of the summit crater or caldera of a volcano, false where it holds none.
bool summitBasin(const VolcanoSite& s, const Detail& d, Basin& b) {
    const VolcanoOffset top = volcanoSummit(s);
    const auto crater = static_cast<double>(s.crater);
    const int rays = raysOf(s, d);
    const std::size_t stride = kSteps + 1;
    std::vector<double> heights(static_cast<std::size_t>(rays) * stride);
    const double center = alone(s, d, top.dx, top.dz);
    double spill = kInf;
    for (int k = 0; k < rays; ++k) {
        const double turns = static_cast<double>(k) / rays;
        const double c = dmath::cosTurns(turns);
        const double sn = dmath::sinTurns(turns);
        double* h = &heights[static_cast<std::size_t>(k) * stride];
        h[0] = center;
        double crest = center;
        for (int i = 1; i <= kSteps; ++i) {
            const double r = kOuter * crater * i / kSteps;
            h[i] = alone(s, d, top.dx + r * c, top.dz + r * sn);
            crest = std::max(crest, h[i]);
        }
        spill = std::min(spill, crest);
    }
    // On each ray, its crest and the radius where the inner wall rises to the spill below it.
    b.within.resize(static_cast<std::size_t>(rays));
    b.crest.resize(static_cast<std::size_t>(rays));
    double floor = center;
    for (int k = 0; k < rays; ++k) {
        const double* h = &heights[static_cast<std::size_t>(k) * stride];
        const auto crest = static_cast<int>(std::max_element(h, h + stride) - h);
        int i = crest;
        while (i > 0 && !(h[i - 1] < spill)) {
            --i;
        }
        b.within[static_cast<std::size_t>(k)] = static_cast<float>(kOuter * crater * i / kSteps);
        b.crest[static_cast<std::size_t>(k)] =
            static_cast<float>(kOuter * crater * crest / kSteps);
        floor = std::min(floor, *std::min_element(h, h + i + 1));
    }
    b.x = top.dx;
    b.z = top.dz;
    b.wetX = top.dx;
    b.wetZ = top.dz;
    b.radius = crater;
    b.floor = floor;
    b.spill = spill - freeboardOf(static_cast<double>(s.craterDepth));
    b.lake = true;
    return b.spill > b.floor + kLeast;
}

// The rim of a maar: points on a ring around it and the height of the relief there.
struct Rim {
    std::vector<double> x;
    std::vector<double> z;
    std::vector<double> height;
};

// Whether two maars cut each other's rims.
bool touch(const Basin& a, const Basin& b) {
    return dmath::hypot(a.x - b.x, a.z - b.z) < a.radius + b.radius;
}

// The lake of a group of maars whose rims cut one another (one maar alone, often): the lowest
// point of their rims out of the others, their lowest floor, the climate of the first.
void shareLake(std::vector<Basin>& basins, const std::vector<Rim>& rims,
               const std::vector<double>& depths, std::size_t first,
               const std::vector<std::size_t>& group) {
    double rim = kInf;
    double floor = kInf;
    double depth = 0.0;
    for (const std::size_t m : group) {
        const Rim& r = rims[m];
        for (std::size_t k = 0; k < r.x.size(); ++k) {
            const bool inOther = std::any_of(group.begin(), group.end(), [&](std::size_t o) {
                return o != m &&
                       dmath::hypot(r.x[k] - basins[o].x, r.z[k] - basins[o].z) < basins[o].radius;
            });
            if (!inOther) {
                rim = std::min(rim, r.height[k]);
            }
        }
        floor = std::min(floor, basins[m].floor);
        depth = std::max(depth, depths[m]);
    }
    for (const std::size_t m : group) {
        basins[m].floor = floor;
        basins[m].spill = rim - freeboardOf(depth);
        basins[m].wetX = basins[first].x;
        basins[m].wetZ = basins[first].z;
    }
}

// The basins of the maars of a field that hold water under a wet climate (three in four, as in
// the Eifel), their rims read over the real ground around them.
void maarBasins(const VolcanoSite& s, Detail& d, const volcano_relief::GroundAt& ground) {
    const auto real = [&](double dx, double dz) {
        const volcano_relief::Ground g = ground(s.x + dx, s.z + dz);
        Point p;
        p.height = g.height;
        p.regional = g.regional;
        volcano_relief::apply(s, d, dx, dz, 0.0, p);
        return p.height;
    };
    std::vector<Basin> basins;
    std::vector<Rim> rims;
    std::vector<double> depths;
    for (std::size_t i = 0; i < d.cones.size(); ++i) {
        const volcano_relief::Cone& c = d.cones[i];
        if (!c.maar || !(unit(s.seed, kMaarLake + i) < 0.75)) {
            continue;
        }
        Rim rim;
        for (int k = 0; k < kMaarRays; ++k) {
            const double turns = static_cast<double>(k) / kMaarRays;
            rim.x.push_back(c.x + c.radius * dmath::cosTurns(turns));
            rim.z.push_back(c.z + c.radius * dmath::sinTurns(turns));
            rim.height.push_back(real(rim.x.back(), rim.z.back()));
        }
        Basin b;
        b.x = c.x;
        b.z = c.z;
        b.radius = c.radius;
        b.floor = real(c.x, c.z);
        for (int k = 0; k < 8; ++k) {
            const double turns = (k + 0.5) / 8.0;
            b.floor = std::min(b.floor, real(c.x + 0.4 * c.radius * dmath::cosTurns(turns),
                                             c.z + 0.4 * c.radius * dmath::sinTurns(turns)));
        }
        b.lake = true;
        b.maar = true;
        basins.push_back(b);
        rims.push_back(std::move(rim));
        depths.push_back(c.height);
    }
    // The groups of maars whose rims cut one another, each from its first maar.
    std::vector<std::size_t> groupOf(basins.size());
    for (std::size_t i = 0; i < basins.size(); ++i) {
        groupOf[i] = i;
        for (std::size_t j = 0; j < i; ++j) {
            if (touch(basins[i], basins[j])) {
                const std::size_t from = groupOf[i];
                for (std::size_t k = 0; k <= i; ++k) {
                    groupOf[k] = groupOf[k] == from ? groupOf[j] : groupOf[k];
                }
            }
        }
    }
    const std::size_t offset = d.basins.size();
    for (std::size_t first = 0; first < basins.size(); ++first) {
        std::vector<std::size_t> group;
        for (std::size_t m = 0; m < basins.size(); ++m) {
            if (groupOf[m] == groupOf[first]) {
                group.push_back(m);
            }
        }
        if (group.front() != first) {
            continue;
        }
        shareLake(basins, rims, depths, first, group);
        for (const std::size_t m : group) {
            basins[m].group = offset + first;
        }
    }
    d.basins.insert(d.basins.end(), basins.begin(), basins.end());
}

} // namespace

void addBasins(const VolcanoSite& s, Detail& d, const volcano_relief::GroundAt& ground) {
    if (s.crater > 0.0f && unit(s.seed, kLake) < lakeChance(s, d)) {
        Basin b;
        if (summitBasin(s, d, b)) {
            b.group = d.basins.size();
            d.basins.push_back(std::move(b));
        }
    }
    if (s.form == VolcanoForm::MaarField) {
        maarBasins(s, d, ground);
    }
}

double lakeLevel(const Basin& b, double humidity, bool& salt) {
    salt = false;
    if (!b.lake) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double fill = vf::smoothstep(kDry, kWet, humidity);
    const double level = b.floor + fill * (b.spill - b.floor);
    if (!(level > b.floor + kLeast)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    salt = humidity < kSalt;
    return level;
}

bool within(const Basin& b, double dx, double dz) {
    const double ox = dx - b.x;
    const double oz = dz - b.z;
    const double r2 = ox * ox + oz * oz;
    if (b.within.empty()) {
        return r2 < b.radius * b.radius;
    }
    const double outer = kOuter * b.radius;
    if (!(r2 < outer * outer)) {
        return false;
    }
    const auto rays = static_cast<double>(b.within.size());
    double turns = r2 > 0.0 ? dmath::atan2(oz, ox) / vf::kTwoPi : 0.0;
    turns -= std::floor(turns);
    const double t = turns * rays;
    const double i = std::floor(t);
    const auto i0 = static_cast<std::size_t>(i) % b.within.size();
    const std::size_t i1 = (i0 + 1) % b.within.size();
    const double limit = static_cast<double>(b.within[i0]) +
                         (t - i) * static_cast<double>(b.within[i1] - b.within[i0]);
    return r2 < limit * limit;
}

bool drained(const Basin& b, const std::function<bool(double, double)>& carved) {
    if (b.crest.empty()) {
        for (int k = 0; k < kMaarRays; ++k) {
            const double turns = static_cast<double>(k) / kMaarRays;
            for (const double q : {1.0, 1.15}) {
                if (carved(b.x + q * b.radius * dmath::cosTurns(turns),
                           b.z + q * b.radius * dmath::sinTurns(turns))) {
                    return true;
                }
            }
        }
        return false;
    }
    const auto rays = static_cast<double>(b.crest.size());
    for (std::size_t k = 0; k < b.crest.size(); ++k) {
        const double turns = static_cast<double>(k) / rays;
        const auto r = static_cast<double>(b.crest[k]);
        if (carved(b.x + r * dmath::cosTurns(turns), b.z + r * dmath::sinTurns(turns))) {
            return true;
        }
    }
    return false;
}

} // namespace terrain::volcano_water
