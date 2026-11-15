#include "sea_plateaus.hpp"

#include "sea_floor.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace terrain::sea_plateaus {
namespace {

constexpr std::uint64_t kStream = 0x9A7EA05ULL;
// At sea: the middle of a plateau at least this far from any land (the islands included).
constexpr double kOffshore = 20'000.0;
// The step of the gradient of the distance to the large lands, the seaward direction.
constexpr double kGradientStep = 2.0 * sea_shore::kShoreCell;
// The rim of the top rounded over this many meters each side of the outline (the top inside is
// exact).
constexpr double kRim = 6'000.0;
// No floor of the seas lies deeper than this (the deepest trenches): the reach of a flank.
constexpr double kDeepestFloor = 11'000.0;
// The amplitudes of the bends of the outline over SeaParams::marginalBend, k = 1, 2, 3.
constexpr std::array<double, 3> kBendShares = {1.0, 0.7, 0.5};

double unitOf(std::uint64_t h) {
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double draw(std::uint64_t cell, std::uint64_t k) {
    return unitOf(noise::deriveSeed(cell, k));
}

// The seaward direction at (x, z): up the gradient of the distance to the large lands, away from
// them. False where it has none.
bool seawardAt(const sea_shore::Shore& shore, const WorldSize& world, double x, double z,
               double& outX, double& outZ) {
    const auto big = [&](double dx, double dz) {
        return sea_shore::distancesAt(shore, wrapCoordinate(x + dx, world.x),
                                      wrapCoordinate(z + dz, world.z))
            .big;
    };
    const double gx = big(kGradientStep, 0.0) - big(-kGradientStep, 0.0);
    const double gz = big(0.0, kGradientStep) - big(0.0, -kGradientStep);
    const double length = dmath::hypot(gx, gz);
    if (!(length > 0.0)) {
        return false;
    }
    outX = gx / length;
    outZ = gz / length;
    return true;
}

// The shape drawn for a plateau of cell `cell` whose middle is set: everything but where it is.
void drawShape(std::uint64_t cell, const SeaParams& p, Plateau& q) {
    const double size = draw(cell, 3);
    q.radius = lerp(p.marginalRadiusLow, p.marginalRadiusHigh, size * size);
    q.stretch = lerp(1.0, p.marginalStretch, draw(cell, 4));
    const dmath::SineCosine turn = dmath::sinCosTurns(draw(cell, 5));
    q.cosine = turn.cosine;
    q.sine = turn.sine;
    double bends = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        q.bend[k] = p.marginalBend * kBendShares[k] * draw(cell, 6 + k);
        const dmath::SineCosine phase = dmath::sinCosTurns(draw(cell, 9 + k));
        q.bendCos[k] = phase.cosine;
        q.bendSin[k] = phase.sine;
        bends += q.bend[k];
    }
    const double depth = draw(cell, 12);
    q.top = lerp(p.marginalTopLow, p.marginalTopHigh, depth * depth);
    q.drop = p.marginalTiltDrop * draw(cell, 13);
    q.shallowest = p.marginalTopLow;
    q.flank = lerp(p.marginalFlankLow, p.marginalFlankHigh, draw(cell, 14));
    // The flank is measured across the squeezed ellipse: stretch times longer along its axis.
    q.reach = q.stretch *
              (q.radius * (1.0 + bends) + kRim + (kDeepestFloor - q.shallowest) / q.flank);
}

// The plateau of cell (i, j), if it holds one that stays.
std::optional<Plateau> plateauOf(std::uint64_t seed, const WorldSize& world, const SeaParams& p,
                                 const sea_shore::Shore& shore, const sea_plates::Plates& plates,
                                 int i, int j, int cellsX) {
    const std::uint64_t cell = noise::deriveSeed(
        noise::deriveSeed(seed, kStream),
        static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(cellsX) +
            static_cast<std::uint64_t>(i));
    if (!(draw(cell, 0) < p.marginalShare)) {
        return std::nullopt;
    }
    Plateau q;
    q.x = -0.5 * world.x + (static_cast<double>(i) + draw(cell, 1)) * kCell;
    q.z = -0.5 * world.z + (static_cast<double>(j) + draw(cell, 2)) * kCell;
    const sea_shore::Distances d = sea_shore::distancesAt(shore, q.x, q.z);
    if (d.any < kOffshore || d.big < p.marginalNear || d.big > p.marginalFar) {
        return std::nullopt;
    }
    if (!(sea_plates::splineAt(plates, plates.active, q.x, q.z) <
          static_cast<double>(p.activeLow))) {
        return std::nullopt; // an active margin
    }
    if (!seawardAt(shore, world, q.x, q.z, q.seaX, q.seaZ)) {
        return std::nullopt;
    }
    drawShape(cell, p, q);
    return q;
}

// Lists each plateau in every cell its reach touches.
void indexOf(Plateaus& s) {
    const auto cells = static_cast<std::size_t>(s.cellsX) * static_cast<std::size_t>(s.cellsZ);
    std::vector<std::vector<std::uint32_t>> lists(cells);
    for (std::size_t k = 0; k < s.list.size(); ++k) {
        const Plateau& q = s.list[k];
        const auto first = [&](double v, double half) {
            return static_cast<int>(std::floor((v + half) / kCell));
        };
        const int i0 = first(q.x - q.reach, 0.5 * s.world.x);
        const int i1 = first(q.x + q.reach, 0.5 * s.world.x);
        const int j0 = first(q.z - q.reach, 0.5 * s.world.z);
        const int j1 = first(q.z + q.reach, 0.5 * s.world.z);
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const auto wi = static_cast<std::size_t>((i % s.cellsX + s.cellsX) % s.cellsX);
                const auto wj = static_cast<std::size_t>((j % s.cellsZ + s.cellsZ) % s.cellsZ);
                std::vector<std::uint32_t>& list =
                    lists[wj * static_cast<std::size_t>(s.cellsX) + wi];
                if (list.empty() || list.back() != static_cast<std::uint32_t>(k)) {
                    list.push_back(static_cast<std::uint32_t>(k));
                }
            }
        }
    }
    s.start.assign(cells + 1, 0);
    for (std::size_t c = 0; c < cells; ++c) {
        s.start[c + 1] = s.start[c] + static_cast<std::uint32_t>(lists[c].size());
    }
    s.members.reserve(s.start[cells]);
    for (const std::vector<std::uint32_t>& list : lists) {
        s.members.insert(s.members.end(), list.begin(), list.end());
    }
}

} // namespace

std::size_t Plateaus::memoryBytes() const {
    return sizeof(Plateaus) + list.capacity() * sizeof(Plateau) +
           (start.capacity() + members.capacity()) * sizeof(std::uint32_t);
}

Plateaus build(std::uint64_t seed, const WorldSize& world, const SeaParams& p,
               const sea_shore::Shore& shore, const sea_plates::Plates& plates) {
    Plateaus s;
    s.world = world;
    s.cellsX = static_cast<int>(std::llround(world.x / kCell));
    s.cellsZ = static_cast<int>(std::llround(world.z / kCell));
    if (p.marginalShare > 0.0) {
        for (int j = 0; j < s.cellsZ; ++j) {
            for (int i = 0; i < s.cellsX; ++i) {
                if (const std::optional<Plateau> q =
                        plateauOf(seed, world, p, shore, plates, i, j, s.cellsX)) {
                    s.list.push_back(*q);
                }
            }
        }
    }
    indexOf(s);
    return s;
}

double elevationOf(const Plateau& q, const WorldSize& world, double x, double z) {
    const double dx = wrapCoordinate(x - q.x, world.x);
    const double dz = wrapCoordinate(z - q.z, world.z);
    if (dx * dx + dz * dz > q.reach * q.reach) {
        return kNone; // below every floor of the seas
    }
    // In the frame of the ellipse, its long axis squeezed to the short one.
    const double along = (dx * q.cosine + dz * q.sine) / q.stretch;
    const double across = -dx * q.sine + dz * q.cosine;
    const double r = dmath::hypot(along, across);
    double outline = q.radius;
    if (r > 0.0) {
        // cos(k a) and sin(k a) of the angle a of the point, from its cosine and sine.
        const double c = along / r;
        const double s = across / r;
        const std::array<double, 3> ck = {c, c * c - s * s, c * (4.0 * c * c - 3.0)};
        const std::array<double, 3> sk = {s, 2.0 * c * s, s * (3.0 - 4.0 * s * s)};
        double bend = 1.0;
        for (std::size_t k = 0; k < 3; ++k) {
            bend += q.bend[k] * (ck[k] * q.bendCos[k] - sk[k] * q.bendSin[k]);
        }
        outline *= bend;
    }
    const double outside = sea_floor::roundedMax(r - outline, 0.0, kRim);
    // Its top sinks seaward across it, never above its shallowest.
    const double seaward =
        std::clamp((dx * q.seaX + dz * q.seaZ) / (q.radius * q.stretch), -1.0, 1.0);
    const double depth = std::max(q.shallowest, q.top + 0.5 * q.drop * seaward);
    return -depth - q.flank * outside;
}

double elevationAt(const Plateaus& s, double x, double z) {
    if (s.list.empty()) {
        return kNone;
    }
    const int i = std::clamp(static_cast<int>(std::floor((x + 0.5 * s.world.x) / kCell)), 0,
                             s.cellsX - 1);
    const int j = std::clamp(static_cast<int>(std::floor((z + 0.5 * s.world.z) / kCell)), 0,
                             s.cellsZ - 1);
    const std::size_t c = static_cast<std::size_t>(j) * static_cast<std::size_t>(s.cellsX) +
                          static_cast<std::size_t>(i);
    double best = kNone;
    for (std::uint32_t k = s.start[c]; k < s.start[c + 1]; ++k) {
        best = std::max(best, elevationOf(s.list[s.members[k]], s.world, x, z));
    }
    return best;
}

} // namespace terrain::sea_plateaus
