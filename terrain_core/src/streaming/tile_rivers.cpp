#include "tile_rivers.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace terrain::tile {

namespace {

bool inland(WaterKind kind) {
    return kind == WaterKind::River || kind == WaterKind::Lake || kind == WaterKind::SaltLake;
}

// A segment of a course, downstream from a to b, at the images nearest to the tile.
struct Segment {
    double ax = 0.0;
    double az = 0.0;
    double bx = 0.0;
    double bz = 0.0;
};

// The box of the tile in its canonical range, meters, and its center.
struct Box {
    double minX = 0.0;
    double minZ = 0.0;
    double side = 0.0;
    [[nodiscard]] double centerX() const { return minX + 0.5 * side; }
    [[nodiscard]] double centerZ() const { return minZ + 0.5 * side; }
    [[nodiscard]] bool holds(double x, double z) const {
        return x >= minX && x < minX + side && z >= minZ && z < minZ + side;
    }
};

// The segments of the courses of reaches that come within kFlowReach of the box.
std::vector<Segment> segmentsNear(const std::vector<RiverReach>& reaches, const Box& box,
                                  const WorldSize& world) {
    std::vector<Segment> segments;
    const double low = -kFlowReach;
    const double high = box.side + kFlowReach;
    for (const RiverReach& reach : reaches) {
        for (std::size_t i = 0; i + 1 < reach.points.size(); ++i) {
            Segment s;
            s.ax = nearestImage(reach.points[i].x, box.centerX(), world.x);
            s.az = nearestImage(reach.points[i].z, box.centerZ(), world.z);
            s.bx = nearestImage(reach.points[i + 1].x, s.ax, world.x);
            s.bz = nearestImage(reach.points[i + 1].z, s.az, world.z);
            const double x0 = std::min(s.ax, s.bx) - box.minX;
            const double x1 = std::max(s.ax, s.bx) - box.minX;
            const double z0 = std::min(s.az, s.bz) - box.minZ;
            const double z1 = std::max(s.az, s.bz) - box.minZ;
            if (x1 >= low && x0 <= high && z1 >= low && z0 <= high) {
                segments.push_back(s);
            }
        }
    }
    return segments;
}

// The direction downstream of the segment nearest to (x, z) (the first of the nearest), or 0.
void flowAt(const std::vector<Segment>& segments, double x, double z, WaterColumn& column) {
    double best = std::numeric_limits<double>::infinity();
    const Segment* nearest = nullptr;
    for (const Segment& s : segments) {
        const double dx = s.bx - s.ax;
        const double dz = s.bz - s.az;
        const double length2 = dx * dx + dz * dz;
        double t = length2 > 0.0 ? ((x - s.ax) * dx + (z - s.az) * dz) / length2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double ex = s.ax + t * dx - x;
        const double ez = s.az + t * dz - z;
        const double d2 = ex * ex + ez * ez;
        if (d2 < best && length2 > 0.0) {
            best = d2;
            nearest = &s;
        }
    }
    if (nearest != nullptr) {
        const double dx = nearest->bx - nearest->ax;
        const double dz = nearest->bz - nearest->az;
        const double length = dmath::hypot(dx, dz);
        column.flowX = static_cast<float>(dx / length);
        column.flowZ = static_cast<float>(dz / length);
    }
}

// The falls of reaches whose lip lies in the box.
std::vector<TileFall> fallsIn(const std::vector<RiverReach>& reaches, const Box& box,
                              const WorldSize& world) {
    std::vector<TileFall> falls;
    for (const RiverReach& reach : reaches) {
        for (const RiverFall& f : reach.falls) {
            TileFall fall;
            fall.x = nearestImage(f.x, box.centerX(), world.x);
            fall.z = nearestImage(f.z, box.centerZ(), world.z);
            if (!box.holds(fall.x, fall.z)) {
                continue;
            }
            fall.footX = nearestImage(f.footX, fall.x, world.x);
            fall.footZ = nearestImage(f.footZ, fall.z, world.z);
            fall.top = f.top;
            fall.bottom = f.bottom;
            fall.width = f.width;
            falls.push_back(fall);
        }
    }
    return falls;
}

} // namespace

void addRiverWater(TileData& data, const Heightfield& field, const TileLattice& lattice,
                   const TileColumns& columns) {
    const Hydrology* hydrology = field.hydrology();
    if (hydrology == nullptr ||
        std::none_of(columns.waterKinds.begin(), columns.waterKinds.end(), inland)) {
        return;
    }
    data.waterColumns.resize(columns.waterKinds.size());
    bool river = false;
    for (std::size_t n = 0; n < data.waterColumns.size(); ++n) {
        data.waterColumns[n].kind = columns.waterKinds[n];
        data.waterColumns[n].level = columns.waterLevels[n];
        river = river || columns.waterKinds[n] == WaterKind::River;
    }
    if (!river) {
        return; // lakes only: no flow, and every fall lies in a river
    }
    const Box box{static_cast<double>(lattice.x0), static_cast<double>(lattice.z0),
                  static_cast<double>(lattice.levelHeight)};
    const double radius = box.side * 0.7072 + kFlowReach; // half the diagonal, and beyond
    std::vector<RiverReach> reaches;
    for (const std::size_t r : hydrology->reachesNear(box.centerX(), box.centerZ(), radius)) {
        reaches.push_back(hydrology->reach(r));
    }
    const std::vector<Segment> segments = segmentsNear(reaches, box, field.world());
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            WaterColumn& column = data.waterColumns[static_cast<std::size_t>(k * kGridSamples + i)];
            if (column.kind == WaterKind::River) {
                flowAt(segments, box.minX + i * lattice.spacing, box.minZ + k * lattice.spacing,
                       column);
            }
        }
    }
    data.falls = fallsIn(reaches, box, field.world());
}

} // namespace terrain::tile
