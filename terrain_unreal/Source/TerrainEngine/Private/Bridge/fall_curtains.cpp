#include "fall_curtains.hpp"

#include "fall_edges.hpp"
#include "fall_foam.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/water_kind.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace terrain::unreal {

namespace {

// The water above a lip ends where its curtain hangs, never in front of it (fall_edges.hpp).
static_assert(kEdgeOver <= kCurtainAhead);

constexpr double kRowDrop = 4.0;   // meters of the drop between two rows of a curtain, at most
constexpr int kMostRows = 64;
constexpr double kFoamAbove = 0.05; // meters over the water below
// The foam lies on the cells under the water of the pool below its fall: their corners within
// kFoamLevelLeast meters plus kFoamLevelCells cells of its level.
constexpr double kFoamLevelLeast = 0.25;
constexpr double kFoamLevelCells = 0.35;

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

EdgePoint edgePointAt(const TileFall& f, std::size_t p, double t) {
    return {f.edgeX[p] + t * (f.edgeX[p + 1] - f.edgeX[p]),
            f.edgeZ[p] + t * (f.edgeZ[p + 1] - f.edgeZ[p]),
            f.footEdgeX[p] + t * (f.footEdgeX[p + 1] - f.footEdgeX[p]),
            f.footEdgeZ[p] + t * (f.footEdgeZ[p + 1] - f.footEdgeZ[p])};
}

// The edge within the channel of the fall (TileFall::width across about its lip), cut where it
// leaves it.
EdgeRun channelEdge(const TileFall& f, double dirX, double dirZ) {
    const double half = 0.5 * static_cast<double>(f.width);
    std::array<double, kFallEdge> across{};
    for (std::size_t p = 0; p < kFallEdge; ++p) {
        across[p] = (f.edgeX[p] - f.x) * -dirZ + (f.edgeZ[p] - f.z) * dirX;
    }
    EdgeRun points;
    for (std::size_t p = 0; p + 1 < kFallEdge; ++p) {
        const double a = across[p];
        const double b = across[p + 1];
        const double low = std::max(std::min(a, b), -half);
        const double high = std::min(std::max(a, b), half);
        if (!(low <= high)) {
            continue; // outside the channel
        }
        const double span = b - a;
        const double tLow = span != 0.0 ? (low - a) / span : 0.0;
        const double tHigh = span != 0.0 ? (high - a) / span : 1.0;
        const double in = std::min(tLow, tHigh);
        if (points.empty() || in > 0.0) {
            points.push_back(edgePointAt(f, p, in));
        }
        points.push_back(edgePointAt(f, p, std::max(tLow, tHigh)));
    }
    return points;
}

} // namespace

std::vector<EdgeRun> curtainRuns(const TileFall& f, double dirX, double dirZ) {
    const double from = static_cast<double>(f.wetFrom);
    const double to = static_cast<double>(f.wetTo);
    if (!(to > from)) {
        return {channelEdge(f, dirX, dirZ)};
    }
    const auto at = [&f](double s) {
        const double p = std::clamp(std::floor(s), 0.0, static_cast<double>(kFallEdge) - 2.0);
        return edgePointAt(f, static_cast<std::size_t>(p), s - p);
    };
    EdgeRun run{at(from)};
    for (double p = std::floor(from) + 1.0; p < to; p += 1.0) {
        run.push_back(at(p));
    }
    run.push_back(at(to));
    return {run};
}

namespace {

// The curtain of a fall along a run of the edge of its lip (curtainRuns), in the frame of the
// tile: from each point of the run down to where its water meets the pool (footEdgeX),
// kCurtainAhead in front, curved or straight as curtain; uv0.x meters along the run.
void shapedCurtain(Writer& out, const TileFall& f, const EdgeRun& e,
                   double originX, double originZ, double lipY) {
    const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
    const int rows = std::clamp(static_cast<int>(std::ceil(drop / kRowDrop)), 2, kMostRows);
    std::vector<double> across(e.size(), 0.0);
    for (std::size_t p = 1; p < e.size(); ++p) {
        across[p] = across[p - 1] + dmath::hypot(e[p][0] - e[p - 1][0], e[p][1] - e[p - 1][1]);
    }
    const double width = across.back();
    const double lipDx = f.footX - f.x;
    const double lipDz = f.footZ - f.z;
    const double lipRun = std::max(dmath::hypot(lipDx, lipDz), 1e-6);
    std::vector<std::int32_t> previous(e.size(), -1);
    std::vector<std::int32_t> current(e.size(), -1);
    for (int r = 0; r <= rows; ++r) {
        const double t = static_cast<double>(r) / rows;
        const double fall = drop * t + (r == rows ? kCurtainBelow : 0.0);
        for (std::size_t p = 0; p < e.size(); ++p) {
            const double dx = e[p][2] - e[p][0];
            const double dz = e[p][3] - e[p][1];
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
            current[p] = out.vertex({e[p][0] - originX + dirX * ahead, lipY - fall,
                                     e[p][1] - originZ + dirZ * ahead},
                                    normal, uv, {kWaterCurtain, static_cast<float>(drop)}, width);
            if (r > 0 && p > 0) {
                out.triangle(previous[p - 1], previous[p], current[p]);
                out.triangle(previous[p - 1], current[p], current[p - 1]);
            }
        }
        std::swap(previous, current);
    }
}

// Whether the column (i, k) of the pool of a foam (pool(i, k)) stands inside it: its neighbors
// (the eight about it, those along the side of the tile on a side, none at a corner: the same in
// the tile beside) in the pool too. On its border, the foam fades out to nothing: never a step
// of the cells of the tile at its edge.
template <typename Pool>
bool inside(const Pool& pool, int i, int k) {
    const bool sideX = i == 0 || i == kChunkCells;
    const bool sideZ = k == 0 || k == kChunkCells;
    for (int dk = -1; dk <= 1; ++dk) {
        for (int di = -1; di <= 1; ++di) {
            const bool along = (sideX && di != 0) || (sideZ && dk != 0);
            const int ni = i + di;
            const int nk = k + dk;
            if (along || ni < 0 || nk < 0 || ni > kChunkCells || nk > kChunkCells) {
                continue;
            }
            if (!pool(ni, nk)) {
                return false;
            }
        }
    }
    return true;
}

// The foam s of fall f in the frame of the tile (fall_foam.hpp), on the cells of the tile whose
// four corners stand in the water of its pool (levels: drawnWaterLevels, within kFoamLevelLeast
// plus kFoamLevelCells cells of the water below the fall) and reach its ellipse, kFoamAbove over
// it: uv0 its offset from the center (foamOffset), uv1.v 0 on the border of the pool (inside)
// and 1 within, uv2.v its downstream semi-axis (the material fades it out to a ragged rim and
// toward the border).
void poolFoam(Writer& out, const TileData& data, const std::vector<double>& levels,
              const TileFall& f, const FoamShape& s) {
    const double spacing = tileCellMeters(data.request.key.lod);
    const double side = kChunkCells * spacing;
    const double nearX = std::clamp(s.centerX, 0.0, side) - s.centerX;
    const double nearZ = std::clamp(s.centerZ, 0.0, side) - s.centerZ;
    if (!(dmath::hypot(nearX, nearZ) <= std::max(s.along, s.down))) {
        return; // the foam does not reach the tile
    }
    const double tolerance = kFoamLevelLeast + kFoamLevelCells * spacing;
    const auto pool = [&](int i, int k) {
        const double level = levels[static_cast<std::size_t>(k * kGridSamples + i)];
        return level > static_cast<double>(kNoWaterLevel) &&
               std::abs(level - static_cast<double>(f.bottom)) <= tolerance;
    };
    // Within the ellipse: the nearest point of the cell to its center, in the frame of the foam.
    const auto reaches = [&](int i, int k) {
        const double x = std::clamp(s.centerX, i * spacing, (i + 1) * spacing);
        const double z = std::clamp(s.centerZ, k * spacing, (k + 1) * spacing);
        const std::array<double, 2> o = foamOffset(s, x, z);
        return dmath::hypot(o[0], o[1]) <= s.down;
    };
    std::vector<std::int32_t> index(levels.size(), -1);
    const auto vertexAt = [&](int i, int k) {
        std::int32_t& v = index[static_cast<std::size_t>(k * kGridSamples + i)];
        if (v < 0) {
            const double x = i * spacing;
            const double z = k * spacing;
            const double y = levels[static_cast<std::size_t>(k * kGridSamples + i)];
            const UnrealUV kind{kWaterFoam, inside(pool, i, k) ? 1.0f : 0.0f};
            const std::array<double, 2> o = foamOffset(s, x, z);
            v = out.vertex({x, y - data.originY + kFoamAbove, z}, {0.0, 1.0, 0.0},
                           {static_cast<float>(o[0]), static_cast<float>(o[1])}, kind, s.down);
        }
        return v;
    };
    for (int k = 0; k < kChunkCells; ++k) {
        for (int i = 0; i < kChunkCells; ++i) {
            if (!reaches(i, k) || !pool(i, k) || !pool(i + 1, k) ||
                !pool(i, k + 1) || !pool(i + 1, k + 1)) {
                continue;
            }
            const std::int32_t a = vertexAt(i, k);
            const std::int32_t b = vertexAt(i + 1, k);
            const std::int32_t c = vertexAt(i, k + 1);
            const std::int32_t d = vertexAt(i + 1, k + 1);
            out.triangle(a, d, b);
            out.triangle(a, c, d);
        }
    }
}

} // namespace

UnrealWaterMesh buildFallCurtains(const TileData& data) {
    UnrealWaterMesh mesh;
    Writer out(mesh);
    out.setAltitude(data.originY);
    const std::vector<double> levels =
        data.falls.empty() ? std::vector<double>{} : drawnWaterLevels(data);
    const FallEdges edges(data);
    for (const TileFall& f : data.falls) {
        const double drop = static_cast<double>(f.top) - static_cast<double>(f.bottom);
        const double dx = f.footX - f.x;
        const double dz = f.footZ - f.z;
        const double run = dmath::hypot(dx, dz);
        if (!(drop > 0.0) || !(run > 0.0)) {
            continue; // no fall (its foot lies below and downstream)
        }
        // Its curtain in the tile of its lip.
        const Point lip{f.x - data.originX, static_cast<double>(f.top) - data.originY,
                        f.z - data.originZ};
        if (f.edge == kFallEdge) {
            for (const EdgeRun& edge : f.here ? curtainRuns(f, dx / run, dz / run)
                                              : std::vector<EdgeRun>{}) {
                if (edge.size() >= 2) {
                    shapedCurtain(out, f, edge, data.originX, data.originZ, lip.y);
                }
            }
        } else if (f.here) {
            curtain(out, f, lip, dx / run, dz / run, run);
        }
        // Its foam on the pool, in every tile it reaches.
        if (!levels.empty()) {
            poolFoam(out, data, levels, f, foamShape(f, data, edges));
        }
    }
    return mesh;
}

} // namespace terrain::unreal
