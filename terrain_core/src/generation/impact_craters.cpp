// The impact craters of agent 52 (impact_craters.hpp).
#include "impact_craters.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/pre_climate.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace terrain::lows {

namespace {

// The counts of the classes on the world of the game (kCraterDiameters), the cumulative exponent
// of the power law of their diameters.
constexpr std::array<double, kCraterClasses> kCounts = {200.0, 80.0, 15.0};
constexpr double kPowerLaw = 1.8;
constexpr double kGameArea = WorldSize{}.x * WorldSize{}.z;
// At most kDraws candidates per crater sought.
constexpr int kDraws = 60;
// Over the footprint: no relief, no belt, kInland meters from the sea at least; kApart meters
// between the reaches of two lows.
constexpr float kPlain = 0.11f;
constexpr double kInland = 40'000.0;
constexpr double kApart = 20'000.0;
// The harmonics of the outline (orders 2, 3, 4) and of the crest (order 6): their ranges.
constexpr std::array<double, 4> kHarmonicLow = {0.02, 0.005, 0.005, 0.1};
constexpr std::array<double, 4> kHarmonicHigh = {0.07, 0.04, 0.06, 0.35};
// The wear: 1 - exp(-kWearRate * age * rate), the age skewed toward old, the rate growing with
// the humidity and the softness of the rock (1 - the character of the geography).
constexpr double kWearBase = 0.15;
constexpr double kWearHumid = 2.5;
constexpr double kWearSoft = 0.4;
constexpr double kWearRate = 2.2;
constexpr float kMeanHumidity = 0.35f;

double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double wrapped(double d, double period) {
    return d - period * std::floor(d / period + 0.5);
}

int targetOf(std::size_t cls, const WorldSize& w, double density) {
    return static_cast<int>(std::lround(kCounts[cls] * density * (w.x * w.z) / kGameArea));
}

// The diameter of a class at the share u of its truncated power law.
double diameterOf(std::size_t cls, double u) {
    const double low = kCraterDiameters[cls];
    const double tail = dmath::pow(kCraterDiameters[cls + 1] / low, -kPowerLaw);
    return low * dmath::pow(1.0 - u * (1.0 - tail), -1.0 / kPowerLaw);
}

bool onPlain(const GeographySample& p) {
    return p.coast >= kInland && p.relief <= kPlain && p.belt <= 0.0f;
}

bool clearOf(const std::vector<LowSite>& taken, const std::vector<Crater>& craters,
             const WorldSize& w, const Crater& c) {
    const auto apart = [&](double x, double z, double reach) {
        const double dx = wrapped(c.x - x, w.x);
        const double dz = wrapped(c.z - z, w.z);
        const double gap = c.reach + reach + kApart;
        return dx * dx + dz * dz >= gap * gap;
    };
    return std::all_of(taken.begin(), taken.end(),
                       [&](const LowSite& s) { return apart(s.x, s.z, s.reach); }) &&
           std::all_of(craters.begin(), craters.end(),
                       [&](const Crater& o) { return apart(o.x, o.z, o.reach); });
}

bool footprintTaken(const Geography& g, const Exclusion& excluded, const Crater& c) {
    for (const auto& [x, z] : footprintOf(c.x, c.z, c.reach)) {
        if (!onPlain(g.sampleAt(x, z))) {
            return false;
        }
    }
    return footprintFree(excluded, c.x, c.z, c.reach);
}

std::optional<Crater> candidate(std::uint64_t a, const Geography& g, const PreClimate* climate,
                                std::size_t cls) {
    const WorldSize& w = g.world();
    const double x = (static_cast<double>(a & 0xFFFF'FFFFU) / 4294967296.0 - 0.5) * w.x;
    const double z = (static_cast<double>(a >> 32U) / 4294967296.0 - 0.5) * w.z;
    const GeographySample p = g.sampleAt(x, z);
    if (!onPlain(p)) {
        return std::nullopt;
    }
    const std::uint64_t b = noise::deriveSeed(a, 1);
    const std::uint64_t e = noise::deriveSeed(a, 2);
    const std::uint64_t f = noise::deriveSeed(a, 3);
    Crater c = freshCrater(diameterOf(cls, field(b, 0)), field(b, 1));
    c.x = x;
    c.z = z;
    for (std::size_t k = 0; k < 4; ++k) {
        c.harmonics[k] =
            kHarmonicLow[k] + (kHarmonicHigh[k] - kHarmonicLow[k]) * field(e, static_cast<int>(k));
        c.phases[k] = field(k < 2 ? b : f, static_cast<int>(2 + k % 2));
    }
    const float humidity = climate != nullptr ? climate->humidityAt(x, z) : kMeanHumidity;
    c.wear = craterWear(field(f, 0), humidity, p.character);
    c.reach = craterReach(c);
    return c;
}

} // namespace

double craterWear(double ageDraw, float humidity, float character) {
    const double age = 1.0 - (1.0 - ageDraw) * (1.0 - ageDraw);
    const double rate = kWearBase + kWearHumid * static_cast<double>(humidity) +
                        kWearSoft * (1.0 - static_cast<double>(character));
    return 1.0 - dmath::exp(-kWearRate * age * rate);
}

int craterClass(double diameter) {
    int k = 0;
    while (k + 1 < kCraterClasses &&
           diameter >= kCraterDiameters[static_cast<std::size_t>(k + 1)]) {
        ++k;
    }
    return k;
}

Craters makeCraters(std::uint64_t seed, const Geography& geography, const PreClimate* climate,
                    double density, const std::vector<LowSite>& taken,
                    const Exclusion& excluded) {
    const WorldSize& world = geography.world();
    Craters out;
    for (std::size_t cls = kCraterClasses; cls-- > 0;) {
        const int target = targetOf(cls, world, density);
        const std::uint64_t stream = noise::deriveSeed(seed, cls);
        int accepted = 0;
        for (int draw = 0; draw < kDraws * target && accepted < target; ++draw) {
            const std::optional<Crater> c = candidate(
                noise::deriveSeed(stream, static_cast<std::uint64_t>(draw)), geography, climate,
                cls);
            if (c && clearOf(taken, out.sites, world, *c) &&
                footprintTaken(geography, excluded, *c)) {
                out.sites.push_back(*c);
                ++accepted;
            }
        }
    }
    out.reach = ReachLattice(world, 65'536.0);
    for (std::size_t k = 0; k < out.sites.size(); ++k) {
        out.reach.add(static_cast<std::uint32_t>(k), out.sites[k].x, out.sites[k].z,
                      out.sites[k].reach);
    }
    out.reach.finish();
    return out;
}

CraterShape craterAt(const Craters& craters, const WorldSize& world, double x, double z) {
    for (const std::uint32_t k : craters.reach.at(x, z)) {
        const Crater& c = craters.sites[k];
        const CraterShape s = shapeAt(c, wrapped(x - c.x, world.x), wrapped(z - c.z, world.z));
        if (s.delta != 0.0 || s.inside > 0.0) {
            return s;
        }
    }
    return {};
}

} // namespace terrain::lows
