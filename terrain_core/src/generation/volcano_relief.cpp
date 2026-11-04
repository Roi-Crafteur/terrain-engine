// The relief of a volcano at a point (volcano_relief.hpp): its edifice over the ground, its crater
// or caldera (with its breach), the amphitheater of a collapse, its radial ravines; then its
// fields, flows, cones and maars (volcano_fields.cpp).
#include "volcano_relief.hpp"

#include "terrain/det_math.hpp"
#include "volcano_fields.hpp"
#include "volcano_forms.hpp"
#include "volcano_sites.hpp"
#include "volcano_water.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain::volcano_relief {

namespace vf = volcano_forms;

namespace {

// The streams of the noises of a volcano (from its seed).
enum Stream : std::uint64_t {
    kOutlineNoise = 101,
    kRimNoise,
    kGaps,
    kFloorNoise,
    kCoarseRavines,
    kFineRavines,
    kSwell,
    kWallNoise,
    kGullies,
    kRimSwell,
    kTalus,
};

// Beyond the foot of an edifice its apron falls away by this slope (14 degrees): it fills the
// valleys at its foot only near it, its edge following the ground, never a circle.
constexpr double kApronSlope = 0.25;

double profileAt(const VolcanoSite& s, const Shape& sh, double rho) {
    return profileOf(s, sh, rho);
}

// The stretched frame of a point around the summit of an edifice: the long axis shrunk to the
// short one, the summit shifted off the center (fading to the foot).
struct Frame {
    double u = 0.0;
    double v = 0.0;
    double r = 0.0;
    double turns = 0.0;
    double radius = 0.0; // the radius of the foot along the azimuth of the point
    double rho = 0.0;
    double rho0 = 0.0; // before the shift and the outline: whether the edifice can reach
};

Frame frameOf(const VolcanoSite& s, const Shape& sh, double dx, double dz) {
    Frame f;
    const double radius = static_cast<double>(s.radius);
    f.u = (dx * sh.cosAxis + dz * sh.sinAxis) / static_cast<double>(s.elongation);
    f.v = -dx * sh.sinAxis + dz * sh.cosAxis;
    f.rho0 = dmath::hypot(f.u, f.v) / radius;
    const double shift = 1.0 - vf::smoothstep(0.0, 1.0, f.rho0);
    f.u -= sh.offsetU * shift;
    f.v -= sh.offsetV * shift;
    f.r = dmath::hypot(f.u, f.v);
    f.turns = f.r > 0.0 ? dmath::atan2(f.v, f.u) / vf::kTwoPi : 0.0;
    f.turns -= std::floor(f.turns);
    double outline = 1.0 + sh.lobe1 * dmath::cosTurns(f.turns - sh.phase1) +
                     sh.lobe2 * dmath::cosTurns(2.0 * (f.turns - sh.phase2)) +
                     sh.ragged * vf::ringNoise(s.seed + kOutlineNoise, f.turns, sh.cells);
    for (int k = 0; k < sh.arms; ++k) {
        const auto a = static_cast<std::size_t>(k);
        const double t = (f.turns - sh.armTurns[a] - std::round(f.turns - sh.armTurns[a])) /
                         sh.armWidth;
        if (std::abs(t) < 1.0) {
            const double bump = 1.0 - t * t;
            outline += sh.armReach[a] * bump * bump;
        }
    }
    f.radius = radius * outline;
    f.rho = f.r / f.radius;
    return f;
}

// How much higher or lower the rim of a caldera stands along the azimuth of the frame: up to 0.3
// of its depth, over two layers of noise of 5 and 17 cells (no period); 0 for the craters of the
// cones. The rim of an island caldera the sea does not breach never sinks below kDryRim: the sea
// stays out of it. And the band of the flank it lifts or lowers, around the rim.
constexpr double kDryRim = 40.0;

double rimSwell(const VolcanoSite& s, const Shape& sh, const Frame& f) {
    const auto depth = static_cast<double>(s.craterDepth);
    if (s.form != VolcanoForm::Caldera && s.form != VolcanoForm::PlateauCaldera &&
        s.form != VolcanoForm::IslandCaldera) {
        return 0.0;
    }
    const double swell = 0.3 * depth *
                         (0.65 * vf::ringNoise(s.seed + kRimSwell, f.turns, 5) +
                          0.35 * vf::ringNoise(s.seed + kRimSwell + 1, f.turns, 17));
    if (s.form == VolcanoForm::IslandCaldera && !sh.breached) {
        return std::max(swell, kDryRim - static_cast<double>(s.height));
    }
    return swell;
}

double rimBand(const VolcanoSite& s, const Frame& f) {
    const double t = (f.rho - static_cast<double>(s.crater) / f.radius) / 0.3;
    const double u = std::max(0.0, 1.0 - t * t);
    return u * u;
}

// The outline of a crater along the azimuth of the frame, a factor of its mean radius: spurs and
// re-entrants over two layers of noise (11 and 29 cells).
double craterOutline(const VolcanoSite& s, const Shape& sh, const Frame& f) {
    return 1.0 + sh.rimNoise * (vf::ringNoise(s.seed + kRimNoise, f.turns, 11) +
                                0.5 * vf::ringNoise(s.seed + kRimNoise + 7, f.turns, 29));
}

// Where the profile of an edifice is read at the frame. An island caldera rises from the sea
// floor, kilometers under its rim, so steeply that the outlines of its foot and of its rim would
// lift or sink its rim by hundreds of meters: its rim, wherever the outline puts it, reads the
// profile at its mean radius (it stands at its height all around, VolcanoSite::height, never
// sunk under the sea), and the outline of its foot stretches its flanks from there.
double shapedRho(const VolcanoSite& s, const Shape& sh, const Frame& f) {
    if (s.form != VolcanoForm::IslandCaldera) {
        return f.rho;
    }
    const auto radius = static_cast<double>(s.radius);
    const double outline = craterOutline(s, sh, f);
    const double rim = static_cast<double>(s.crater) * outline;
    const double t = vf::smoothstep(rim / f.radius, 1.0, f.rho);
    return f.r / (radius * outline + (f.radius - radius * outline) * t);
}

// The radius of the foot the profile of the rim is read against (shapedRho).
double rimRho(const VolcanoSite& s, const Frame& f, double crater) {
    return s.form == VolcanoForm::IslandCaldera
               ? static_cast<double>(s.crater) / static_cast<double>(s.radius)
               : crater / f.radius;
}

// The surface of the edifice at the frame, absolute.
double edificeAt(const VolcanoSite& s, const Shape& sh, const Frame& f, double base, double rise) {
    if (!(f.rho < 1.0)) {
        // The slope of the profile at the foot (meters per meter), then steepening to that of the
        // apron over 0.2 radius: no crease along the foot.
        const double k = sh.sharpness;
        const double foot = volcano_sites::formRule(s.form).shield
                                ? 0.0
                                : rise / f.radius * k * dmath::exp(-k) / (1.0 - dmath::exp(-k));
        const double t = f.rho - 1.0;
        const double bend = std::max(0.0, kApronSlope - foot) / 0.4;
        return base - f.radius * t * (foot + bend * std::min(t, 0.2)) -
               f.radius * kApronSlope * std::max(0.0, t - 0.2);
    }
    double p = profileAt(s, sh, shapedRho(s, sh, f));
    if (sh.swell > 0.0 && p > 0.0) {
        // The swell of the flanks, none at the foot (the coasts of an island wander) nor around
        // the crater (its rim stays level around its floor).
        const double crater = static_cast<double>(s.crater) / f.radius;
        p += sh.swell / rise * std::sqrt(p) *
             vf::smoothstep(1.5 * crater, 3.0 * crater, f.rho) *
             vf::planeNoise(s.seed + kSwell, f.u / 3500.0, f.v / 3500.0);
    }
    if (s.form == VolcanoForm::TwinStrato) {
        // The second summit, lower, on the other side of the long axis.
        const double rho2 = dmath::hypot(f.u + 0.55 * f.radius, f.v) / (0.7 * f.radius);
        p = vf::smax(p, 0.8 * profileAt(s, sh, rho2), 0.04);
    }
    return base + rise * p + rimSwell(s, sh, f) * rimBand(s, f);
}

// The rim of the crater along the azimuth of the frame: the edifice at its radius, and the gaps
// of the rim of a drowned caldera breached by the sea.
double rimAt(const VolcanoSite& s, const Shape& sh, const Frame& f, double crater, double base,
             double rise) {
    double rim = base + rise * profileAt(s, sh, rimRho(s, f, crater)) + rimSwell(s, sh, f);
    if (s.form == VolcanoForm::IslandCaldera && sh.breached) {
        const double gap =
            vf::smoothstep(0.3, 0.75, 0.65 * vf::ringNoise(s.seed + kGaps, f.turns, 7) +
                                          0.35 * vf::ringNoise(s.seed + kGaps + 1, f.turns, 17));
        rim -= (rim + 40.0) * gap;
    }
    return rim;
}

// The couloirs down the wall of a crater, at irregular spacings along its rim (cells of about 180 m,
// about one in two holding one), deepest at mid wall: gullies and chutes, never a smooth cylinder.
double gulliesAt(const VolcanoSite& s, const Frame& f, double crater, double q, double wall) {
    const double t = (q - (1.0 - 0.5 * wall)) / (0.6 * wall);
    if (!(std::abs(t) < 1.0)) {
        return 0.0;
    }
    const int cells = std::max(16, static_cast<int>(vf::kTwoPi * crater / 180.0));
    const double x = f.turns * cells + 0.3 * vf::lineNoise(s.seed + kGullies + 1, q * 6.0);
    const dmath::FloorIndex i = dmath::floorIndex(x);
    const auto cell = static_cast<std::uint64_t>(((i.index % cells) + cells) % cells);
    const double center = 0.3 + 0.4 * volcano_sites::unit(s.seed + kGullies, cell);
    const double depth = 0.05 * static_cast<double>(s.craterDepth) *
                         (0.2 + 0.8 * volcano_sites::unit(s.seed + kGullies, cell + 100'000));
    // About one cell in two holds a couloir: no fixed spacing.
    if (!(volcano_sites::unit(s.seed + kGullies, cell + 300'000) < 0.55)) {
        return 0.0;
    }
    const double width = 0.12 + 0.28 * volcano_sites::unit(s.seed + kGullies, cell + 200'000);
    const double across = std::max(0.0, 1.0 - std::abs(x - i.value - center) / width);
    return depth * across * across * (1.0 - t * t);
}

void digCrater(const VolcanoSite& s, const Shape& sh, const Frame& f, double base, double rise,
               Point& p) {
    const auto mean = static_cast<double>(s.crater);
    const double crater = mean * craterOutline(s, sh, f);
    const double q = f.r / crater;
    if (!(q < 1.25) && !sh.breached) {
        return;
    }
    const double floor = base + rise * profileAt(s, sh, mean / static_cast<double>(s.radius)) -
                         static_cast<double>(s.craterDepth) +
                         0.015 * static_cast<double>(s.craterDepth) *
                             vf::planeNoise(s.seed + kFloorNoise, f.u / 1500.0, f.v / 1500.0);
    const double rim = rimAt(s, sh, f, crater, base, rise);
    if (q < 1.25) {
        // The wall steeper here, gentler there, cut by couloirs.
        const double wall = std::clamp(
            sh.wall * (1.0 + 0.45 * vf::ringNoise(s.seed + kWallNoise, f.turns, 13) +
                       0.25 * vf::ringNoise(s.seed + kWallNoise + 1, f.turns, 31)),
            0.04, 0.95);
        // The talus at its foot: a fan rising from the floor toward the wall, higher here.
        const double talus = 0.14 * static_cast<double>(s.craterDepth) *
                             (0.7 + 0.3 * vf::ringNoise(s.seed + kTalus, f.turns, 23));
        const double fan = vf::smoothstep(1.0 - 1.5 * wall, 1.0 - 0.4 * wall, q);
        const double foot = floor + talus * fan * fan;
        // Its crest notched here and there, by up to a sixth of its depth, from within.
        const double notch = 0.16 * static_cast<double>(s.craterDepth) *
                             std::min(0.0, vf::ringNoise(s.seed + kRimNoise + 50, f.turns, 9)) *
                             (1.0 - vf::smoothstep(0.92, 1.0, q));
        const double c = foot + (rim + notch - foot) * vf::craterWall(q, wall) -
                         gulliesAt(s, f, crater, q, wall);
        const double weight = 1.0 - vf::smoothstep(1.0, 1.25, q);
        p.height += weight * (vf::smin(p.height, c, 0.04 * static_cast<double>(s.craterDepth) +
                                                        2.0) -
                              p.height);
        p.touched = true;
        p.enclosed = p.enclosed || (q < 1.0 && !(sh.breached && sh.breachBottom < 0.0));
    }
    // The breach: a notch across the wall down to its bottom, opening onto the outer flank; that
    // of an island caldera, a strait with a flat bottom cut down the flank out to the sea, which
    // floods the caldera through it.
    const bool strait = s.form == VolcanoForm::IslandCaldera && sh.breachBottom < 0.0;
    p.flooded = p.flooded || (strait && sh.breached && q < 1.0);
    if (!sh.breached || !(q > 0.3) || (!strait && !(q < 1.3))) {
        return;
    }
    const double across =
        vf::angleBetween(vf::kTwoPi * f.turns, sh.breachAngle) / sh.breachWidth;
    if (!(std::abs(across) < 1.0)) {
        return;
    }
    const double level = sh.breachBottom - std::max(0.0, q - 1.0) * crater * 0.15;
    const double side = strait ? vf::smoothstep(0.5, 1.0, std::abs(across)) : across * across;
    const double notch = level + (std::max(rim, p.height) - level) * side;
    const double weight = strait ? 1.0 : 1.0 - vf::smoothstep(1.1, 1.3, q);
    p.height += weight * (std::min(p.height, notch) - p.height);
    p.flooded = p.flooded || (strait && p.height < 0.0);
}

// The horseshoe amphitheater of a collapse, open down the flank toward collapseAngle.
void digCollapse(const VolcanoSite& s, const Shape& sh, const Frame& f, double base, double rise,
                 Point& p) {
    const double width = static_cast<double>(s.crater);
    const double c = dmath::cos(sh.collapseAngle);
    const double sn = dmath::sin(sh.collapseAngle);
    const double a = f.u * c + f.v * sn;
    const double b = -f.u * sn + f.v * c;
    // Down the flank the amphitheater widens a little and ends by the middle of the flank.
    const double length = 0.55 * f.radius;
    const double open = width * (1.0 + 0.3 * vf::smoothstep(0.0, length, a));
    const double q = a < 0.0 ? dmath::hypot(a, b) / width : std::abs(b) / open;
    if (!(q < 1.25) || !(a < length)) {
        return;
    }
    // Its floor lies craterDepth under its back rim, falling gently down the flank.
    const double rim = base + rise * profileOf(s, sh, width / f.radius);
    const double floor = rim - static_cast<double>(s.craterDepth) - std::max(a, 0.0) * 0.1;
    if (!(floor < p.height)) {
        return;
    }
    const double wall = floor + (p.height - floor) * vf::craterWall(q, 0.35);
    const double weight =
        (1.0 - vf::smoothstep(1.0, 1.25, q)) * (1.0 - vf::smoothstep(0.75 * length, length, a));
    p.height += weight * (std::min(p.height, wall) - p.height);
    p.touched = true;
}

// One layer of ravines: cells cells around, each with its own place, width, depth, start and
// end down the flank (none reaches the summit, few the foot), wandering with rho and deepening
// downslope. Its mean over a cell where the footprint is too coarse for it.
double ravineLayer(std::uint64_t seed, const Frame& f, int cells, double footprint) {
    const double x = f.turns * cells + 0.3 * vf::lineNoise(seed + 1, f.rho * 4.0);
    const dmath::FloorIndex i = dmath::floorIndex(x);
    const auto cell = static_cast<std::uint64_t>(((i.index % cells) + cells) % cells);
    const double start = 0.12 + 0.33 * volcano_sites::unit(seed, cell + 300'000);
    const double end = 0.7 + 0.35 * volcano_sites::unit(seed, cell + 400'000);
    const double along = vf::smoothstep(start, start + 0.25, f.rho) *
                         (1.0 - vf::smoothstep(end - 0.15, end, f.rho));
    if (!(along > 0.0)) {
        return 0.0;
    }
    const double center = 0.3 + 0.4 * volcano_sites::unit(seed, cell);
    const double width = 0.1 + 0.18 * volcano_sites::unit(seed, cell + 100'000);
    const double depth = 0.3 + 0.7 * volcano_sites::unit(seed, cell + 200'000);
    const double t = std::max(0.0, 1.0 - std::abs(x - i.value - center) / width);
    const double point = depth * along * t * std::sqrt(t);
    const double cellWidth = vf::kTwoPi * f.r / cells;
    const double filtered =
        footprint > 0.0 ? vf::smoothstep(0.1, 0.3, footprint / std::max(cellWidth, 1.0)) : 0.0;
    return point + (0.8 * width * depth * along - point) * filtered;
}

void carveRavines(const VolcanoSite& s, const Shape& sh, const Frame& f, double dominance,
                  double footprint, Point& p) {
    if (sh.ravines <= 0 || !(dominance > 0.0)) {
        return;
    }
    // Out of the crater and its rim.
    const double rim = static_cast<double>(s.crater) / f.radius;
    const double out = s.crater > 0.0f ? vf::smoothstep(1.2 * rim, 1.8 * rim, f.rho) : 1.0;
    const double coarse = out * (1.0 - vf::smoothstep(0.9, 1.1, f.rho));
    const double fine = out * vf::smoothstep(0.18, 0.3, f.rho) *
                        (1.0 - vf::smoothstep(0.6, 0.8, f.rho));
    if (!(coarse + fine > 0.0)) {
        return;
    }
    double cut = 0.0;
    if (coarse > 0.0) {
        cut += coarse * ravineLayer(s.seed + kCoarseRavines, f, sh.ravines, footprint);
    }
    if (fine > 0.0) {
        cut += 0.35 * fine * ravineLayer(s.seed + kFineRavines, f, 3 * sh.ravines + 1, footprint);
    }
    p.height -= sh.ravineDepth * cut * dominance;
    p.touched = true;
}

void applyEdifice(const VolcanoSite& s, const Detail& d, const Frame& f, double footprint,
                  bool details, Point& p) {
    const Shape& sh = d.shape;
    const volcano_sites::FormRule rule = volcano_sites::formRule(s.form);
    const double base = edificeBase(s, d);
    const double rise = edificeRise(s, sh);
    const double v = edificeAt(s, sh, f, base, rise);
    const double cover = 1.0 - vf::smoothstep(1.0, rule.apron, f.rho);
    if (!(cover > 0.0)) {
        return;
    }
    const double old = p.height;
    p.height = old + cover * (vf::smax(old, v, sh.smoothing) - old);
    p.touched = true;
    const double dominance = cover * vf::smoothstep(-sh.smoothing, sh.smoothing, v - old);
    p.cover = std::max(p.cover, static_cast<float>(dominance));
    if (s.form == VolcanoForm::CollapsedStrato) {
        digCollapse(s, sh, f, base, rise, p);
    } else if (s.crater > 0.0f && s.form != VolcanoForm::Seamount) {
        digCrater(s, sh, f, base, rise, p);
    }
    if (details) {
        carveRavines(s, sh, f, dominance, footprint, p);
    }
}

} // namespace

double profileOf(const VolcanoSite& s, const Shape& sh, double rho) {
    return volcano_sites::formRule(s.form).shield ? vf::shieldProfile(rho)
                                                 : vf::stratoProfile(rho, sh.sharpness);
}

double edificeBase(const VolcanoSite& s, const Detail& d) {
    return s.atSea ? static_cast<double>(s.floor) : d.base;
}

double edificeRise(const VolcanoSite& s, const Shape& sh) {
    const double h = static_cast<double>(s.height);
    const double rise = s.atSea ? std::max(300.0, h - static_cast<double>(s.floor)) : h;
    if (s.form != VolcanoForm::Caldera && s.form != VolcanoForm::IslandCaldera) {
        return rise;
    }
    const double rim = profileOf(s, sh, static_cast<double>(s.crater) /
                                            static_cast<double>(s.radius));
    return rise / std::max(rim, 0.05);
}

void apply(const VolcanoSite& s, const Detail& d, double dx, double dz, double footprint,
           Point& p) {
    const volcano_sites::FormRule rule = volcano_sites::formRule(s.form);
    if (rule.edifice) {
        const Frame f = frameOf(s, d.shape, dx, dz);
        if (f.rho0 < rule.apron * (1.0 + rule.outline) + 0.02) {
            applyEdifice(s, d, f, footprint, true, p);
        }
    } else {
        volcano_fields::applySheet(s, d, dx, dz, footprint, p);
    }
    volcano_fields::applyFlows(s, d, dx, dz, footprint, p);
    volcano_fields::applyCones(s, d, dx, dz, p);
    volcano_water::applySprings(d, dx, dz, footprint, p);
}

double surfaceBeforeFlows(const VolcanoSite& s, const Detail& d, double dx, double dz,
                          const Ground& ground) {
    Point p;
    p.height = ground.height;
    p.regional = ground.regional;
    if (volcano_sites::formRule(s.form).edifice) {
        const Frame f = frameOf(s, d.shape, dx, dz);
        applyEdifice(s, d, f, 0.0, false, p);
    } else {
        volcano_fields::applySheet(s, d, dx, dz, 0.0, p);
    }
    return p.height;
}

Details::Details(std::size_t count)
    : flags_(std::make_unique<std::once_flag[]>(count)),
      details_(std::make_unique<Detail[]>(count)) {}

const Detail& Details::get(std::size_t index, const VolcanoSite& site,
                           const GroundAt& ground) const {
    std::call_once(flags_[index], [&]() { details_[index] = detailOf(site, ground); });
    return details_[index];
}

} // namespace terrain::volcano_relief
