#include "sea_plates.hpp"

#include "geography_boundaries.hpp"
#include "geography_stages.hpp"
#include "planet/planet_parallel.hpp"
#include "settlement/geology_plates.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace terrain::sea_plates {
namespace {

// A node seeds the ridge of its boundary when the geography raises a ridge there (its field at
// least kSeedRidge) within kSeedReach node spans of the axis.
constexpr float kSeedRidge = 0.2f;
constexpr double kSeedReach = 0.75;
// A margin is active where a trench lies within kActiveReach nodes, blurred over kActiveBlur.
constexpr int kActiveReach = 3;
constexpr int kActiveBlur = 2;

struct Cell {
    int i0 = 0;
    int j0 = 0;
    double tu = 0.0;
    double tv = 0.0;
    double gx = 0.0;
    double gz = 0.0;
};

Cell cellOf(const Plates& p, double x, double z) {
    Cell c;
    c.gx = wrapCoordinate(x + p.anchorX, p.world.x);
    c.gz = wrapCoordinate(z, p.world.z);
    const double u = (c.gx - p.grid.x(0)) / p.grid.sizeX;
    const double v = (c.gz - p.grid.z(0)) / p.grid.sizeZ;
    c.i0 = static_cast<int>(std::floor(u));
    c.j0 = static_cast<int>(std::floor(v));
    c.tu = u - c.i0;
    c.tv = v - c.j0;
    return c;
}

Ridge ridgeOf(const TectonicPlates& plates, int boundary, const geography::Boundary& b) {
    const PlateBoundary& pb = plates.boundaries()[static_cast<std::size_t>(boundary)];
    const Plate& a = plates.plates()[static_cast<std::size_t>(pb.a)];
    const Plate& other = plates.plates()[static_cast<std::size_t>(pb.b)];
    const WorldSize& world = plates.world();
    const double dx = geography::shortOffset(other.x - a.x, world.x);
    const double dz = geography::shortOffset(other.z - a.z, world.z);
    const double length = dmath::hypot(dx, dz);
    Ridge r;
    r.boundary = boundary;
    r.ax = a.x;
    r.az = a.z;
    r.mx = wrapCoordinate(a.x + 0.5 * dx, world.x);
    r.mz = wrapCoordinate(a.z + 0.5 * dz, world.z);
    r.nx = length > 0.0 ? dx / length : 1.0;
    r.nz = length > 0.0 ? dz / length : 0.0;
    r.strength = b.strength;
    r.rate = static_cast<float>(std::clamp((static_cast<double>(b.strength) - geography::kWeakest) /
                                               (1.0 - geography::kWeakest),
                                           0.0, 1.0));
    return r;
}

// The ridges and the nodes on their axes (the seeds of the distances), with their distance to it.
struct Seeds {
    std::vector<std::size_t> node;
    std::vector<double> distance;
    std::vector<std::int16_t> label;
};

using Nears = std::vector<settlement::GeographyPlates::Near>;

Nears nearsOf(const settlement::GeographyPlates& gp, const geography::Grid& g, int threads) {
    Nears near(g.nodes());
    planet::parallelRows(g.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                near[g.index(i, j)] = gp.nearAtNode(i, j);
            }
        }
    });
    return near;
}

Seeds seedRidges(const settlement::GeographyPlates& gp, const geography::Tectonics& t,
                 const Nears& near, Plates& p) {
    const geography::Grid& g = p.grid;
    std::vector<int> ridgeOfBoundary(gp.plates().boundaries().size(), -1);
    const double reach = kSeedReach * std::max(g.sizeX, g.sizeZ);
    Seeds s;
    for (std::size_t k = 0; k < g.nodes(); ++k) {
        const settlement::GeographyPlates::Near& n = near[k];
        if (gp.crust().land[k] != 0 || n.boundary < 0 || t.ridge[k] < kSeedRidge ||
            !(std::abs(n.towardB) < reach)) {
            continue;
        }
        const geography::Boundary& b = gp.boundary(n.boundary);
        if (b.kind != BoundaryKind::Divergence) {
            continue;
        }
        int& ridge = ridgeOfBoundary[static_cast<std::size_t>(n.boundary)];
        if (ridge < 0) {
            ridge = static_cast<int>(p.ridges.size());
            p.ridges.push_back(ridgeOf(gp.plates(), n.boundary, b));
        }
        s.node.push_back(k);
        s.distance.push_back(std::abs(n.towardB));
        s.label.push_back(static_cast<std::int16_t>(ridge));
    }
    return s;
}

// The nearest ridge of each node of the sea through the sea (8 neighbors on the torus): the
// same distances and labels whatever the order of the ties (the queue orders them by node).
void spread(const Seeds& s, const std::vector<std::uint8_t>& land, Plates& p) {
    const geography::Grid& g = p.grid;
    p.distance.assign(g.nodes(), kFar);
    p.label.assign(g.nodes(), -1);
    std::vector<double> best(g.nodes(), static_cast<double>(kFar));
    using Entry = std::pair<double, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (std::size_t k = 0; k < s.node.size(); ++k) {
        const std::size_t n = s.node[k];
        if (s.distance[k] < best[n]) {
            best[n] = s.distance[k];
            p.label[n] = s.label[k];
            queue.push({s.distance[k], n});
        }
    }
    const double diagonal = dmath::hypot(g.sizeX, g.sizeZ);
    while (!queue.empty()) {
        const auto [d, n] = queue.top();
        queue.pop();
        if (d > best[n]) {
            continue;
        }
        const int i = g.column(n);
        const int j = g.row(n);
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const std::size_t next = g.index(i + di, j + dj);
                if ((di == 0 && dj == 0) || land[next] != 0) {
                    continue;
                }
                const double step = di != 0 && dj != 0 ? diagonal : (di != 0 ? g.sizeX : g.sizeZ);
                if (d + step < best[next]) {
                    best[next] = d + step;
                    p.label[next] = p.label[n];
                    queue.push({d + step, next});
                }
            }
        }
    }
    for (std::size_t n = 0; n < g.nodes(); ++n) {
        p.distance[n] = static_cast<float>(std::min(best[n], static_cast<double>(kFar)));
        p.label[n] = land[n] != 0 ? kLand : p.label[n];
    }
}

// The nodes the trench field of a collision reaches (at least kTrenchSeed): its boundary and the
// offset of its axis across it, as geography_tectonics.cpp (collide) puts it, 1.35 half widths
// of the belt beyond its axis on the subducting side; and its peak within kPeakReach nodes.
constexpr float kTrenchSeed = 0.01f;
constexpr double kTrenchBeyond = 1.35;
constexpr int kPeakReach = 2;

void seedTrenches(const settlement::GeographyPlates& gp, const geography::Tectonics& t,
                  const GeographyParams& params, const Nears& near, Plates& p) {
    const geography::Grid& g = p.grid;
    p.trenchLabel.assign(g.nodes(), -1);
    p.trenchAxis.assign(g.nodes(), 0.0f);
    std::vector<int> trenchOfBoundary(gp.plates().boundaries().size(), -1);
    for (int j = 0; j < g.nz; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const std::size_t k = g.index(i, j);
            const settlement::GeographyPlates::Near& n = near[k];
            if (t.trench[k] < kTrenchSeed || n.boundary < 0) {
                continue;
            }
            const geography::Boundary& b = gp.boundary(n.boundary);
            if (b.kind != BoundaryKind::Collision) {
                continue;
            }
            int& trench = trenchOfBoundary[static_cast<std::size_t>(n.boundary)];
            if (trench < 0) {
                trench = static_cast<int>(p.trenches.size());
                Ridge r = ridgeOf(gp.plates(), n.boundary, b);
                const PlateBoundary& pb =
                    gp.plates().boundaries()[static_cast<std::size_t>(n.boundary)];
                r.toward = b.overriding == pb.b ? 1.0f : -1.0f;
                p.trenches.push_back(r);
            }
            const geography::NodeFrame f = gp.fields().at(i, j);
            const double w = b.halfWidth * (1.0 + geography::kWidthSpread * f.width);
            const double axis = params.beltOffset * w + f.meander;
            p.trenchLabel[k] = static_cast<std::int16_t>(trench);
            p.trenchAxis[k] = static_cast<float>(axis - kTrenchBeyond * w);
        }
    }
}

// The largest value of field within reach nodes of each node (rows then columns).
std::vector<float> dilated(const geography::Grid& g, const std::vector<float>& field, int reach) {
    std::vector<float> rows(field.size(), 0.0f);
    for (int j = 0; j < g.nz; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            float most = 0.0f;
            for (int d = -reach; d <= reach; ++d) {
                most = std::max(most, field[g.index(i + d, j)]);
            }
            rows[g.index(i, j)] = most;
        }
    }
    std::vector<float> out(field.size(), 0.0f);
    for (int j = 0; j < g.nz; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            float most = 0.0f;
            for (int d = -reach; d <= reach; ++d) {
                most = std::max(most, rows[g.index(i, j + d)]);
            }
            out[g.index(i, j)] = most;
        }
    }
    return out;
}

// The strongest trench within kActiveReach nodes of each node (rows then columns), blurred.
std::vector<float> activeOf(const geography::Grid& g, const std::vector<float>& trench) {
    std::vector<float> out = dilated(g, trench, kActiveReach);
    geography::boxBlur(g, out, kActiveBlur);
    return out;
}

template <class T> std::size_t bytesOf(const std::vector<T>& v) {
    return v.capacity() * sizeof(T);
}

} // namespace

std::size_t Plates::memoryBytes() const {
    return sizeof(Plates) + bytesOf(offsetX) + bytesOf(offsetZ) + bytesOf(label) +
           bytesOf(distance) + bytesOf(trench) + bytesOf(belt) + bytesOf(active) + bytesOf(ridges) +
           bytesOf(trenchLabel) + bytesOf(trenchAxis) + bytesOf(trenchPeak) + bytesOf(trenches);
}

Plates build(std::uint64_t worldSeed, const Geography& geo, int threads) {
    const int n = planet::resolveThreads(threads);
    const settlement::GeographyPlates gp(worldSeed, geo, n);
    Plates p;
    p.grid = gp.grid();
    p.world = geo.world();
    p.anchorX = geo.anchorX();
    p.offsetX.assign(p.grid.nodes(), 0.0f);
    p.offsetZ.assign(p.grid.nodes(), 0.0f);
    planet::parallelRows(p.grid.nz, n, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < p.grid.nx; ++i) {
                const geography::NodeFrame f = gp.fields().at(i, j);
                p.offsetX[p.grid.index(i, j)] = static_cast<float>(f.warped.x - p.grid.x(i));
                p.offsetZ[p.grid.index(i, j)] = static_cast<float>(f.warped.z - p.grid.z(j));
            }
        }
    });
    geography::Tectonics t =
        geography::buildTectonics(gp.seed(), geo.params(), geo.world(), p.grid, gp.crust(), n);
    const Nears near = nearsOf(gp, p.grid, n);
    spread(seedRidges(gp, t, near, p), gp.crust().land, p);
    seedTrenches(gp, t, geo.params(), near, p);
    p.trenchPeak = dilated(p.grid, t.trench, kPeakReach);
    geography::boxBlur(p.grid, p.trenchPeak, 1);
    p.active = activeOf(p.grid, t.trench);
    p.trench = std::move(t.trench);
    p.belt = std::move(t.belt);
    return p;
}

Point warpedAt(const Plates& p, double x, double z) {
    const Cell c = cellOf(p, x, z);
    const auto blend = [&](const std::vector<float>& f) {
        const auto at = [&](int di, int dj) {
            return static_cast<double>(f[p.grid.index(c.i0 + di, c.j0 + dj)]);
        };
        const double top = at(0, 0) + (at(1, 0) - at(0, 0)) * c.tu;
        const double bottom = at(0, 1) + (at(1, 1) - at(0, 1)) * c.tu;
        return top + (bottom - top) * c.tv;
    };
    return {wrapCoordinate(c.gx + blend(p.offsetX), p.world.x),
            wrapCoordinate(c.gz + blend(p.offsetZ), p.world.z)};
}

Frame frameOf(const Ridge& r, const Point& q, const WorldSize& world) {
    const double dx = geography::shortOffset(q.x - r.mx, world.x);
    const double dz = geography::shortOffset(q.z - r.mz, world.z);
    return {dx * -r.nz + dz * r.nx, dx * r.nx + dz * r.nz};
}

Corners cornersAt(const Plates& p, double x, double z) {
    const Cell c = cellOf(p, x, z);
    Corners out;
    out.node = {p.grid.index(c.i0, c.j0), p.grid.index(c.i0 + 1, c.j0),
                p.grid.index(c.i0, c.j0 + 1), p.grid.index(c.i0 + 1, c.j0 + 1)};
    out.weight = {(1.0 - c.tu) * (1.0 - c.tv), c.tu * (1.0 - c.tv), (1.0 - c.tu) * c.tv,
                  c.tu * c.tv};
    return out;
}

double splineAt(const Plates& p, const std::vector<float>& field, double x, double z) {
    const double gx = wrapCoordinate(x + p.anchorX, p.world.x);
    const double gz = wrapCoordinate(z, p.world.z);
    const geography::Span sx = geography::spanOf(gx, p.grid.periodX(), p.grid.sizeX);
    const geography::Span sz = geography::spanOf(gz, p.grid.periodZ(), p.grid.sizeZ);
    const geography::Spline wx = geography::bspline(sx.t);
    const geography::Spline wz = geography::bspline(sz.t);
    double sum = 0.0;
    for (int b = 0; b < 4; ++b) {
        for (int a = 0; a < 4; ++a) {
            sum += wx.w[static_cast<std::size_t>(a)] * wz.w[static_cast<std::size_t>(b)] *
                   static_cast<double>(field[p.grid.index(sx.node - 1 + a, sz.node - 1 + b)]);
        }
    }
    return sum;
}

} // namespace terrain::sea_plates
