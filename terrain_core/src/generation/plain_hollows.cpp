// The hollows of the plains of agent 52 (plain_hollows.hpp).
#include "plain_hollows.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/pre_climate.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace terrain::lows {

namespace {

constexpr double kTurn = 2.0 * 3.14159265358979323846;

// A kind of hollow: its cells (meters), the largest share of them holding one, its mean radii
// (the small ones the most), depths, largest elongation, lobes (a share of the radius, order 3),
// the share of the relief of the plains its floor keeps.
struct Kind {
    LowKind kind;
    double cell;
    double share;
    double radiusLow;
    double radiusHigh;
    double depthLow;
    double depthHigh;
    double elongation;
    double lobes;
    double keep;
};
constexpr std::array<Kind, 2> kKinds = {{
    {LowKind::Kettle, 768.0, 0.8, 15.0, 100.0, 2.0, 8.0, 1.4, 0.0, 1.0},
    {LowKind::Pan, 16'384.0, 0.7, 400.0, 4'000.0, 2.0, 12.0, 1.8, 0.12, 0.1},
}};
constexpr std::size_t kKettle = 0;
constexpr std::size_t kPan = 1;

// The belts: the kettles under kColdFull C (none over kColdNone), wetter than kWetFull (none under
// kWetNone); the pans drier than kDryFull (P / (P + PET); kElsewhere of them over kDryNone).
constexpr double kColdFull = 3.0;
constexpr double kColdNone = 8.0;
constexpr double kWetNone = 0.2;
constexpr double kWetFull = 0.3;
constexpr double kDryFull = 0.15;
constexpr double kDryNone = 0.3;
constexpr double kElsewhere = 0.03;
// At the center: a plain (kPlain at most: the mountains begin at 0.12), kInland from the sea.
constexpr float kPlain = 0.11f;
constexpr double kInland = 40'000.0;

double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The candidate of a cell (unwrapped indices i, j): its center in the image of the query and on
// the canonical range, its draw and its form. Built in two steps: its seat (hash, draw, center in
// the image of the query: cheap), then the rest once it may reach the point (formOf).
struct Candidate {
    std::uint64_t hash = 0;
    double x = 0.0;
    double z = 0.0;
    double cx = 0.0;
    double cz = 0.0;
    double draw = 1.0;
    double radius = 0.0;
    double depth = 0.0;
    double stretch = 1.0; // the square root of the elongation
    double angle = 0.0;
    double lobes = 0.0;
    double phase = 0.0;
    double reach = 0.0;
};

const std::array<PeriodicCells, 2>& cellsOf(const Hollows& h, std::size_t kind) {
    return kind == kKettle ? h.kettles : h.pans;
}

// The canonical index of a cell: i itself in the canonical range (no division).
std::int64_t canonical(const PeriodicCells& cells, std::int64_t i) {
    const std::int64_t half = cells.count / 2;
    return i >= -half && i < cells.count - half ? i : cells.wrap(i);
}

Candidate seatOf(const Hollows& h, std::size_t kind, std::int64_t i, std::int64_t j) {
    const auto& [cellsX, cellsZ] = cellsOf(h, kind);
    const std::int64_t ci = canonical(cellsX, i);
    const std::int64_t cj = canonical(cellsZ, j);
    const auto key = static_cast<std::uint64_t>(cj + cellsZ.count / 2) *
                         static_cast<std::uint64_t>(cellsX.count) +
                     static_cast<std::uint64_t>(ci + cellsX.count / 2);
    Candidate c;
    c.hash = noise::deriveSeed(h.kindSeeds[kind], key);
    c.draw = field(c.hash, 0);
    const double u = field(c.hash, 1);
    const double v = field(c.hash, 2);
    c.x = (static_cast<double>(i) + u) * cellsX.size;
    c.z = (static_cast<double>(j) + v) * cellsZ.size;
    c.cx = (static_cast<double>(ci) + u) * cellsX.size;
    c.cz = (static_cast<double>(cj) + v) * cellsZ.size;
    return c;
}

void formOf(const Hollows& h, std::size_t kind, Candidate& c) {
    const auto& [cellsX, cellsZ] = cellsOf(h, kind);
    const Kind& k = kKinds[kind];
    c.cx = wrapCoordinate(c.cx, cellsX.period);
    c.cz = wrapCoordinate(c.cz, cellsZ.period);
    const std::uint64_t b = noise::deriveSeed(c.hash, 1);
    const double size = field(c.hash, 3);
    c.radius = k.radiusLow * dmath::pow(k.radiusHigh / k.radiusLow, size * size);
    c.depth = k.depthLow + (k.depthHigh - k.depthLow) * field(b, 0);
    c.stretch = std::sqrt(1.0 + (k.elongation - 1.0) * field(b, 1));
    c.angle = field(b, 2);
    c.lobes = k.lobes * field(b, 3);
    c.phase = field(noise::deriveSeed(c.hash, 2), 0);
    c.reach = c.radius * c.stretch * (1.0 + c.lobes);
}

// The largest reach of a kind.
double reachOf(const Kind& k) {
    return k.radiusHigh * std::sqrt(k.elongation) * (1.0 + k.lobes);
}

// Whether the candidate is laid: on a plain far from the sea, its draw under the share of its
// belt at its center, out of the excluded zone.
bool laid(const Hollows& h, std::size_t kind, const Candidate& c) {
    const GeographySample g = h.geography->sampleAt(c.cx, c.cz);
    if (!(g.relief <= kPlain && g.coast >= kInland && g.belt <= 0.0f)) {
        return false;
    }
    const double temperature = static_cast<double>(h.climate->temperatureAt(c.cx, c.cz));
    const double humidity = static_cast<double>(h.climate->humidityAt(c.cx, c.cz));
    const double belt =
        kind == kKettle ? (1.0 - smooth(kColdFull, kColdNone, temperature)) *
                              smooth(kWetNone, kWetFull, humidity)
                        : std::max(kElsewhere, 1.0 - smooth(kDryFull, kDryNone, humidity));
    return c.draw < kKinds[kind].share * h.density * belt && !(h.excluded && h.excluded(c.cx, c.cz));
}

HollowShape shapeOf(std::size_t kind, const Candidate& c, double dx, double dz) {
    const double along = dx * dmath::cosTurns(c.angle) + dz * dmath::sinTurns(c.angle);
    const double across = dz * dmath::cosTurns(c.angle) - dx * dmath::sinTurns(c.angle);
    double rho = dmath::hypot(along / c.stretch, across * c.stretch) / c.radius;
    if (c.lobes > 0.0 && (dx != 0.0 || dz != 0.0)) {
        const double turns = dmath::atan2(dz, dx) / kTurn;
        rho /= 1.0 + c.lobes * dmath::cosTurns(3.0 * (turns - c.phase));
    }
    HollowShape out;
    if (!(rho < 1.0)) {
        return out;
    }
    out.depth = c.depth;
    out.keep = kKinds[kind].keep;
    if (kind == kKettle) {
        const double w = 1.0 - rho * rho;
        out.profile = w * w;
    } else {
        out.profile = 1.0 - smooth(0.55, 1.0, rho);
        out.flat = 1.0 - smooth(0.45, 0.7, rho);
    }
    return out;
}

} // namespace

Hollows makeHollows(std::uint64_t seed, const Geography& geography, const PreClimate& climate,
                    double density, Exclusion excluded) {
    const WorldSize& world = geography.world();
    Hollows h;
    h.seed = seed;
    h.density = density;
    h.geography = &geography;
    h.climate = &climate;
    h.excluded = std::move(excluded);
    h.kettles = {periodicCells(world.x, kKinds[kKettle].cell),
                 periodicCells(world.z, kKinds[kKettle].cell)};
    h.pans = {periodicCells(world.x, kKinds[kPan].cell), periodicCells(world.z, kKinds[kPan].cell)};
    for (std::size_t kind = 0; kind < kKinds.size(); ++kind) {
        h.kindSeeds[kind] = noise::deriveSeed(seed, kind);
    }
    return h;
}

HollowShape hollowAt(const Hollows& hollows, double x, double z) {
    HollowShape best;
    for (std::size_t kind = 0; kind < kKinds.size(); ++kind) {
        const auto& [cellsX, cellsZ] = cellsOf(hollows, kind);
        const double reach = reachOf(kKinds[kind]);
        const double share = kKinds[kind].share * hollows.density;
        for (std::int64_t j = cellsZ.cellOf(z - reach); j <= cellsZ.cellOf(z + reach); ++j) {
            for (std::int64_t i = cellsX.cellOf(x - reach); i <= cellsX.cellOf(x + reach); ++i) {
                Candidate c = seatOf(hollows, kind, i, j);
                const double dx = x - c.x;
                const double dz = z - c.z;
                if (!(c.draw < share) || !(dx * dx + dz * dz < reach * reach)) {
                    continue;
                }
                formOf(hollows, kind, c);
                if (!(dx * dx + dz * dz < c.reach * c.reach)) {
                    continue;
                }
                const HollowShape s = shapeOf(kind, c, dx, dz);
                if (s.profile * s.depth > best.profile * best.depth && laid(hollows, kind, c)) {
                    best = s;
                }
            }
        }
    }
    return best;
}

std::vector<LowSite> hollowsIn(const Hollows& hollows, double minX, double minZ, double maxX,
                               double maxZ) {
    std::vector<LowSite> out;
    for (std::size_t kind = 0; kind < kKinds.size(); ++kind) {
        const auto& [cellsX, cellsZ] = cellsOf(hollows, kind);
        for (std::int64_t j = cellsZ.cellOf(minZ); j <= cellsZ.cellOf(maxZ); ++j) {
            for (std::int64_t i = cellsX.cellOf(minX); i <= cellsX.cellOf(maxX); ++i) {
                Candidate c = seatOf(hollows, kind, i, j);
                if (!(c.draw < kKinds[kind].share * hollows.density)) {
                    continue;
                }
                formOf(hollows, kind, c);
                if (c.cx >= minX && c.cx < maxX && c.cz >= minZ && c.cz < maxZ &&
                    laid(hollows, kind, c)) {
                    out.push_back({kKinds[kind].kind, c.cx, c.cz, c.radius, c.reach,
                                   static_cast<float>(-c.depth), c.angle, 0.0f});
                }
            }
        }
    }
    return out;
}

} // namespace terrain::lows
