#include "fall_foam.hpp"

#include "fall_curtains.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace terrain::unreal {

namespace {

constexpr double kLeastAxis = 0.5; // meters: the least semi-axis of a foam
constexpr double kLeastDown = 1.5; // meters downstream, before it gets narrower
constexpr double kLevelMatch = 0.25; // the water above a lip at the water below f (meters)
constexpr double kRimStep = 0.5;   // meters between the points of the rim checked
constexpr int kSteps = 12;         // halvings of a semi-axis: 1/4096 of it
constexpr double kPi = 3.14159265358979323846;

struct Point2 {
    double x = 0.0;
    double z = 0.0;
};

// The downstream semi-axis of the foam of f: kFoamRadiusShare of the width of its channel
// (kFoamLeastRadius at least), within its pool (TileFall::pool) when known.
double foamRadius(const TileFall& f) {
    const double radius =
        std::max(kFoamLeastRadius, kFoamRadiusShare * static_cast<double>(f.width));
    if (!(f.pool > 0.0f)) {
        return radius;
    }
    const double room = (static_cast<double>(f.pool) - kCurtainAhead) / (1.0 + kFoamAheadShare);
    return std::max(kLeastAxis, std::min(radius, room));
}

// The ends of the foot of the water falling from f (frame of the tile): the run of its curtain,
// or the band of a straight lip.
std::array<Point2, 2> footOf(const TileFall& f, const TileData& data, double dirX, double dirZ) {
    if (f.edge == kFallEdge) {
        const std::vector<EdgeRun> runs = curtainRuns(f, dirX, dirZ);
        if (!runs.empty() && !runs.front().empty()) {
            const EdgePoint& a = runs.front().front();
            const EdgePoint& b = runs.back().back();
            return {{{a[2] - data.originX, a[3] - data.originZ},
                     {b[2] - data.originX, b[3] - data.originZ}}};
        }
    }
    const double half = 0.5 * kCurtainWidthShare * static_cast<double>(f.width);
    const double x = f.footX - data.originX;
    const double z = f.footZ - data.originZ;
    return {{{x + dirZ * half, z - dirX * half}, {x - dirZ * half, z + dirX * half}}};
}

// The lips the pool of f runs to (FallEdges indices: their water above stands at the water below
// f) that every tile a foam reaching reach meters from (cx, cz) knows.
std::vector<std::size_t> nextLips(const FallEdges& edges, const TileFall& f, double cx, double cz,
                                  double reach) {
    std::vector<std::size_t> out;
    for (std::size_t n = 0; n < edges.size(); ++n) {
        const double top = edges.top(n);
        if (std::abs(top - static_cast<double>(f.bottom)) <= kLevelMatch &&
            top < static_cast<double>(f.top) &&
            edges.lipDistance(n, cx, cz) + reach <= kFallNear) {
            out.push_back(n);
        }
    }
    return out;
}

// The least distance from the ragged rim of the foam s (kFoamRim of its semi-axes) to the edges of
// lips, the points of the rim kRimStep meters apart (the true one less by half of that at most).
double rimClear(const FoamShape& s, const FallEdges& edges, const std::vector<std::size_t>& lips) {
    const double nx = -s.alongZ;
    const double nz = s.alongX;
    const double a = kFoamRim * s.along;
    const double b = kFoamRim * s.down;
    const int points =
        std::max(16, static_cast<int>(std::ceil(2.0 * kPi * std::max(a, b) / kRimStep)));
    double least = std::numeric_limits<double>::infinity();
    for (int p = 0; p < points; ++p) {
        const double angle = 2.0 * kPi * p / points;
        const double u = a * std::cos(angle);
        const double v = b * std::sin(angle);
        const double x = s.centerX + u * s.alongX + v * nx;
        const double z = s.centerZ + u * s.alongZ + v * nz;
        for (const std::size_t n : lips) {
            least = std::min(least, edges.edgeDistance(n, x, z));
        }
    }
    return least;
}

// The largest value in [low, high] for which clear(value) holds, clear(low) holding.
template <typename Clear>
double largest(double low, double high, const Clear& clear) {
    for (int step = 0; step < kSteps; ++step) {
        const double middle = 0.5 * (low + high);
        (clear(middle) ? low : high) = middle;
    }
    return low;
}

} // namespace

FoamShape foamShape(const TileFall& f, const TileData& data, const FallEdges& edges) {
    const double dx = f.footX - f.x;
    const double dz = f.footZ - f.z;
    const double run = std::max(dmath::hypot(dx, dz), 1e-9);
    const double dirX = dx / run;
    const double dirZ = dz / run;
    const std::array<Point2, 2> foot = footOf(f, data, dirX, dirZ);
    const double fx = foot[1].x - foot[0].x;
    const double fz = foot[1].z - foot[0].z;
    const double length = dmath::hypot(fx, fz);
    FoamShape s;
    s.alongX = length > 1e-6 ? fx / length : -dirZ;
    s.alongZ = length > 1e-6 ? fz / length : dirX;
    double nx = -s.alongZ;
    double nz = s.alongX;
    if (nx * dirX + nz * dirZ < 0.0) { // downstream
        nx = -nx;
        nz = -nz;
    }
    s.alongX = nz;
    s.alongZ = -nx;
    s.footHalf = 0.5 * length;
    const double width = static_cast<double>(f.width);
    s.along = std::max(kFoamLeastRadius, s.footHalf + kFoamSideShare * width);
    s.down = std::min(foamRadius(f), s.along);
    const double midX = 0.5 * (foot[0].x + foot[1].x);
    const double midZ = 0.5 * (foot[0].z + foot[1].z);
    // Its center kFoamAheadShare of its downstream semi-axis downstream of the middle of the foot.
    const auto shape = [&](double along, double down) {
        FoamShape t = s;
        t.along = along;
        t.down = down;
        const double ahead = kCurtainAhead + kFoamAheadShare * down;
        t.centerX = midX + nx * ahead;
        t.centerZ = midZ + nz * ahead;
        return t;
    };
    const double along = s.along;
    const double down = s.down;
    s = shape(along, down);
    const std::vector<std::size_t> lips =
        nextLips(edges, f, s.centerX, s.centerZ, kFoamRim * std::max(along, down));
    const auto clear = [&](const FoamShape& t) {
        return rimClear(t, edges, lips) >= kFoamClear + 0.5 * kRimStep;
    };
    if (lips.empty() || clear(s)) {
        return s;
    }
    // Shorter downstream first, then narrower along the foot.
    const double least = std::min(kLeastDown, down);
    if (clear(shape(along, least))) {
        return shape(along, largest(least, down, [&](double d) { return clear(shape(along, d)); }));
    }
    const double narrow = std::min(kLeastAxis, along);
    if (!clear(shape(narrow, least))) {
        return shape(narrow, std::min(least, narrow));
    }
    return shape(largest(narrow, along, [&](double a) { return clear(shape(a, least)); }), least);
}

std::array<double, 2> foamOffset(const FoamShape& s, double x, double z) {
    const double rx = x - s.centerX;
    const double rz = z - s.centerZ;
    const double a = rx * s.alongX + rz * s.alongZ;
    const double squeeze = (s.down / s.along - 1.0) * a;
    return {rx + squeeze * s.alongX, rz + squeeze * s.alongZ};
}

} // namespace terrain::unreal
