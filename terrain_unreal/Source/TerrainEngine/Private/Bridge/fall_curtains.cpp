#include "fall_curtains.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace terrain::unreal {

namespace {

constexpr double kRowDrop = 4.0;   // meters of the drop between two rows of a curtain, at most
constexpr int kMostRows = 64;
constexpr int kFoamSides = 16;
constexpr double kFoamAbove = 0.05; // meters over the water below

// A point in the frame of the tile, meters: (x, z) horizontal, y up.
struct Point {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

class Writer {
public:
    explicit Writer(UnrealWaterMesh& mesh) : mesh_(mesh) {}

    // uv2: the altitude of the point and size, the width of a curtain or the radius of a foam.
    std::int32_t vertex(const Point& p, const Point& normal, UnrealUV uv0, UnrealUV uv1,
                        double size) {
        const auto index = static_cast<std::int32_t>(mesh_.positions.size());
        mesh_.positions.push_back({static_cast<float>(kCentimetersPerMeter * p.x),
                                   static_cast<float>(kCentimetersPerMeter * p.z),
                                   static_cast<float>(kCentimetersPerMeter * p.y)});
        mesh_.normals.push_back({static_cast<float>(normal.x), static_cast<float>(normal.z),
                                 static_cast<float>(normal.y)});
        mesh_.uv0.push_back(uv0);
        mesh_.uv1.push_back(uv1);
        mesh_.uv2.push_back({static_cast<float>(altitude_ + p.y), static_cast<float>(size)});
        return index;
    }

    // A triangle whose front face (Unreal: the side of (c - a) x (b - a)) lies on the side of
    // the normal of its first vertex.
    void triangle(std::int32_t a, std::int32_t b, std::int32_t c) {
        const auto& p = mesh_.positions;
        const UnrealVector3f& pa = p[static_cast<std::size_t>(a)];
        const UnrealVector3f& pb = p[static_cast<std::size_t>(b)];
        const UnrealVector3f& pc = p[static_cast<std::size_t>(c)];
        const double ux = pc.x - pa.x, uy = pc.y - pa.y, uz = pc.z - pa.z;
        const double vx = pb.x - pa.x, vy = pb.y - pa.y, vz = pb.z - pa.z;
        const UnrealVector3f& n = mesh_.normals[static_cast<std::size_t>(a)];
        const double facing = (uy * vz - uz * vy) * n.x + (uz * vx - ux * vz) * n.y +
                              (ux * vy - uy * vx) * n.z;
        for (const std::int32_t index : facing >= 0.0 ? std::array{a, b, c} : std::array{a, c, b}) {
            mesh_.triangles.push_back(index);
        }
    }

    void setAltitude(double originY) { altitude_ = originY; }

private:
    UnrealWaterMesh& mesh_;
    double altitude_ = 0.0;
};

// The curtain of a fall from its lip, in the frame of the tile (y relative to its origin):
// curved (sqrt t) from a lip steeper on average than kCurtainCurveSlope, straight otherwise.
void curtain(Writer& out, const TileFall& f, const Point& lip, double dirX, double dirZ,
             double run) {
    const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
    const double half = 0.5 * kCurtainWidthShare * static_cast<double>(f.width);
    const int rows = std::clamp(static_cast<int>(std::ceil(drop / kRowDrop)), 2, kMostRows);
    const bool curved = drop >= kCurtainCurveSlope * run;
    const double width = 2.0 * half;
    std::int32_t left = -1;
    std::int32_t right = -1;
    for (int r = 0; r <= rows; ++r) {
        const double t = static_cast<double>(r) / rows;
        const double share = curved ? std::sqrt(t) : t;
        const double ahead = kCurtainAhead + run * share;
        const double fall = drop * t + (r == rows ? kCurtainBelow : 0.0);
        const Point center{lip.x + dirX * ahead, lip.y - fall, lip.z + dirZ * ahead};
        // The tangent down the curtain is (run / (2 sqrt t) along dir, -drop) curved, (run along
        // dir, -drop) straight: its normal in the vertical plane of the fall, downstream and up.
        const double along = curved ? run * 0.5 / std::max(share, 0.05) : run;
        const double length = dmath::hypot(drop, along);
        const Point normal{dirX * drop / length, along / length, dirZ * drop / length};
        const UnrealUV kind{kWaterCurtain, static_cast<float>(drop)};
        const auto v = static_cast<float>(fall);
        const std::int32_t l = out.vertex({center.x + dirZ * half, center.y, center.z - dirX * half},
                                          normal, {0.0f, v}, kind, width);
        const std::int32_t rr = out.vertex(
            {center.x - dirZ * half, center.y, center.z + dirX * half}, normal,
            {static_cast<float>(width), v}, kind, width);
        if (r > 0) {
            out.triangle(left, right, rr);
            out.triangle(left, rr, l);
        }
        left = l;
        right = rr;
    }
}

// The curtain of a fall along the edge of its lip (TileFall::edgeX), in the frame of the tile:
// from each point of the edge down to where its water meets the pool (footEdgeX), kCurtainAhead
// in front, curved or straight as curtain; uv0.x meters along the edge. Returns its width.
// The points of an edge (TileFall::edgeX) and of the foot of its water, its two ends carried
// kCurtainSides meters on beyond them: the water of the pools around the ends of the edge falls
// too.
struct CurtainEdge {
    std::array<double, kFallEdge> x{};
    std::array<double, kFallEdge> z{};
    std::array<double, kFallEdge> footX{};
    std::array<double, kFallEdge> footZ{};
};

CurtainEdge curtainEdge(const TileFall& f) {
    CurtainEdge e{f.edgeX, f.edgeZ, f.footEdgeX, f.footEdgeZ};
    for (const auto& [end, next] : {std::pair{std::size_t{0}, std::size_t{1}},
                                    std::pair{kFallEdge - 1, kFallEdge - 2}}) {
        const double dx = f.edgeX[end] - f.edgeX[next];
        const double dz = f.edgeZ[end] - f.edgeZ[next];
        const double length = dmath::hypot(dx, dz);
        if (length > 1e-6) {
            e.x[end] += kCurtainSides * dx / length;
            e.z[end] += kCurtainSides * dz / length;
            e.footX[end] += kCurtainSides * dx / length;
            e.footZ[end] += kCurtainSides * dz / length;
        }
    }
    return e;
}

double shapedCurtain(Writer& out, const TileFall& f, double originX, double originZ,
                     double lipY) {
    const CurtainEdge e = curtainEdge(f);
    const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
    const int rows = std::clamp(static_cast<int>(std::ceil(drop / kRowDrop)), 2, kMostRows);
    std::array<double, kFallEdge> across{};
    for (std::size_t p = 1; p < kFallEdge; ++p) {
        across[p] = across[p - 1] + dmath::hypot(e.x[p] - e.x[p - 1], e.z[p] - e.z[p - 1]);
    }
    const double width = across[kFallEdge - 1];
    const double lipDx = f.footX - f.x;
    const double lipDz = f.footZ - f.z;
    const double lipRun = std::max(dmath::hypot(lipDx, lipDz), 1e-6);
    std::array<std::int32_t, kFallEdge> previous{};
    for (int r = 0; r <= rows; ++r) {
        const double t = static_cast<double>(r) / rows;
        const double fall = drop * t + (r == rows ? kCurtainBelow : 0.0);
        std::array<std::int32_t, kFallEdge> current{};
        for (std::size_t p = 0; p < kFallEdge; ++p) {
            const double dx = e.footX[p] - e.x[p];
            const double dz = e.footZ[p] - e.z[p];
            const double run = dmath::hypot(dx, dz);
            const double dirX = run > 1e-6 ? dx / run : lipDx / lipRun;
            const double dirZ = run > 1e-6 ? dz / run : lipDz / lipRun;
            const bool curved = drop >= kCurtainCurveSlope * run;
            const double share = curved ? std::sqrt(t) : t;
            const double ahead = kCurtainAhead + run * share;
            const double along = std::max(curved ? run * 0.5 / std::max(share, 0.05) : run, 1e-3);
            const double length = dmath::hypot(drop, along);
            const Point normal{dirX * drop / length, along / length, dirZ * drop / length};
            const UnrealUV uv{static_cast<float>(across[p]), static_cast<float>(fall)};
            current[p] = out.vertex({e.x[p] - originX + dirX * ahead, lipY - fall,
                                     e.z[p] - originZ + dirZ * ahead},
                                    normal, uv, {kWaterCurtain, static_cast<float>(drop)}, width);
            if (r > 0 && p > 0) {
                out.triangle(previous[p - 1], previous[p], current[p]);
                out.triangle(previous[p - 1], current[p], current[p - 1]);
            }
        }
        previous = current;
    }
    return width;
}

// The foam at the foot of a fall falling width meters across: kFoamRadiusShare of it in radius
// (kFoamLeastRadius at least), within its pool (TileFall::pool) when known.
double foamRadius(const TileFall& f, double width) {
    const double radius = std::max(kFoamLeastRadius, kFoamRadiusShare * width);
    if (!(f.pool > 0.0f)) {
        return radius;
    }
    const double room = (static_cast<double>(f.pool) - kCurtainAhead) / (1.0 + kFoamAheadShare);
    return std::max(0.5, std::min(radius, room));
}

// The foam on the water below a fall, from where its curtain meets it downstream.
void foam(Writer& out, const TileFall& f, const Point& center, double radius) {
    const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
    const Point up{0.0, 1.0, 0.0};
    const UnrealUV kind{kWaterFoam, static_cast<float>(drop)};
    const std::int32_t middle = out.vertex(center, up, {0.0f, 0.0f}, kind, radius);
    std::int32_t first = -1;
    std::int32_t previous = -1;
    for (int s = 0; s < kFoamSides; ++s) {
        const double turns = static_cast<double>(s) / kFoamSides;
        const double cx = radius * dmath::cosTurns(turns);
        const double cz = radius * dmath::sinTurns(turns);
        const std::int32_t v =
            out.vertex({center.x + cx, center.y, center.z + cz}, up,
                       {static_cast<float>(cx), static_cast<float>(cz)}, kind, radius);
        if (previous >= 0) {
            out.triangle(middle, previous, v);
        } else {
            first = v;
        }
        previous = v;
    }
    out.triangle(middle, previous, first);
}

} // namespace

UnrealWaterMesh buildFallCurtains(const TileData& data) {
    UnrealWaterMesh mesh;
    Writer out(mesh);
    out.setAltitude(data.originY);
    for (const TileFall& f : data.falls) {
        const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
        const double dx = f.footX - f.x;
        const double dz = f.footZ - f.z;
        const double run = dmath::hypot(dx, dz);
        if (!f.here || !(drop > 0.0) || !(run > 0.0)) {
            continue; // drawn by its own tile; or no fall (its foot lies below and downstream)
        }
        const Point lip{f.x - data.originX, static_cast<double>(f.top) - data.originY,
                        f.z - data.originZ};
        double width = static_cast<double>(f.width); // of its channel, or of its edge
        // The foot of the middle of its water.
        double footX = lip.x + dx;
        double footZ = lip.z + dz;
        if (f.edge == kFallEdge) {
            width = shapedCurtain(out, f, data.originX, data.originZ, lip.y);
            footX = f.footEdgeX[kFallEdge / 2] - data.originX;
            footZ = f.footEdgeZ[kFallEdge / 2] - data.originZ;
        } else {
            curtain(out, f, lip, dx / run, dz / run, run);
        }
        const double radius = foamRadius(f, width);
        const double ahead = kCurtainAhead + kFoamAheadShare * radius;
        foam(out, f,
             {footX + dx / run * ahead, static_cast<double>(f.bottom) - data.originY + kFoamAbove,
              footZ + dz / run * ahead},
             radius);
    }
    return mesh;
}

} // namespace terrain::unreal
