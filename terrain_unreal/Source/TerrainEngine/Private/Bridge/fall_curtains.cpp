#include "fall_curtains.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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

// The foam on the water below a fall, around where its curtain meets it.
void foam(Writer& out, const TileFall& f, const Point& center) {
    const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
    const double radius =
        std::max(kFoamLeastRadius, kFoamRadiusShare * static_cast<double>(f.width));
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
        if (!(drop > 0.0) || !(run > 0.0)) {
            continue; // no fall (the foot of a fall of the core lies below and downstream)
        }
        const Point lip{f.x - data.originX, static_cast<double>(f.top) - data.originY,
                        f.z - data.originZ};
        curtain(out, f, lip, dx / run, dz / run, run);
        const double ahead = kCurtainAhead + run;
        foam(out, f,
             {lip.x + dx / run * ahead, static_cast<double>(f.bottom) - data.originY + kFoamAbove,
              lip.z + dz / run * ahead});
    }
    return mesh;
}

} // namespace terrain::unreal
