#include "map_water.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain::map_water {

namespace {

// Beyond the half diagonal of a box, the reaches whose channel may still reach it: the widest
// channels are well under a kilometer.
constexpr double kCourseMargin = 2000.0;

struct PixelRange {
    int i0 = 0;
    int i1 = -1;
    int j0 = 0;
    int j1 = -1;
};

// The pixels of the grid (with its border) whose centers lie in [minX, maxX] x [minZ, maxZ],
// offsets from the west and north edges of the box.
PixelRange pixelsIn(const MapFrame& f, double minX, double maxX, double minZ, double maxZ) {
    const auto first = [](double v, double step) {
        return std::ceil(v / step - 0.5);
    };
    const auto last = [](double v, double step) {
        return std::floor(v / step - 0.5);
    };
    PixelRange r;
    r.i0 = static_cast<int>(std::max(first(minX, f.stepX()), -1.0));
    r.i1 = static_cast<int>(std::min(last(maxX, f.stepX()), static_cast<double>(f.width())));
    r.j0 = static_cast<int>(std::max(first(minZ, f.stepZ()), -1.0));
    r.j1 = static_cast<int>(std::min(last(maxZ, f.stepZ()), static_cast<double>(f.height())));
    return r;
}

struct Segment {
    double ax, az, bx, bz; // offsets from the edges of the box, meters
    double ha, hb;         // half widths, meters
    double meander;        // amplitude of the meanders about the axis, meters
};

// The sample of a pixel at a distance from the axis of a course: covered by a narrow channel, or
// its discharge given to the river samples within the meanders of a wide one.
void layCourse(MapSample& sample, double cover, bool wide, bool within, float discharge) {
    if (wide) {
        if (within && sample.water == WaterKind::River && discharge > sample.discharge) {
            sample.discharge = discharge;
        }
        return;
    }
    const auto c = static_cast<float>(cover);
    if (c > sample.riverCover ||
        (c > 0.0f && c == sample.riverCover && discharge > sample.discharge)) {
        sample.riverCover = c;
        sample.discharge = discharge;
    }
}

// The part of each pixel the channel of a segment covers: 1 within its half width, fading over
// one pixel beyond it.
void drawSegment(const MapFrame& f, const Segment& s, float discharge, std::span<MapSample> out) {
    const double step = std::max(f.stepX(), f.stepZ());
    const bool wide = std::min(s.ha, s.hb) >= 0.5 * kWideChannelPixels * step;
    const double reach = std::max(s.ha, s.hb) + s.meander + step;
    const PixelRange r = pixelsIn(f, std::min(s.ax, s.bx) - reach, std::max(s.ax, s.bx) + reach,
                                  std::min(s.az, s.bz) - reach, std::max(s.az, s.bz) + reach);
    const double dx = s.bx - s.ax;
    const double dz = s.bz - s.az;
    const double length2 = dx * dx + dz * dz;
    for (int j = r.j0; j <= r.j1; ++j) {
        const double pz = (j + 0.5) * f.stepZ();
        for (int i = r.i0; i <= r.i1; ++i) {
            const double px = (i + 0.5) * f.stepX();
            const double t =
                length2 > 0.0
                    ? std::clamp(((px - s.ax) * dx + (pz - s.az) * dz) / length2, 0.0, 1.0)
                    : 0.0;
            const double ex = px - (s.ax + t * dx);
            const double ez = pz - (s.az + t * dz);
            const double half = s.ha + (s.hb - s.ha) * t;
            const double distance = std::sqrt(ex * ex + ez * ez);
            const double cover = std::clamp((half + 0.5 * step - distance) / step, 0.0, 1.0);
            layCourse(out[mapGridIndex(f, i, j)], cover, wide, distance <= reach, discharge);
        }
    }
}

std::uint8_t markAt(double distance, double center, double ring) {
    if (distance <= center) {
        return kMapFallCenter;
    }
    return distance <= ring ? kMapFallRing : std::uint8_t{0};
}

// The falls of a reach (in the order of its course) as stretches: a step whose lip lies within
// stretch meters of the foot of the step before it continues its stretch.
void addStretches(const std::vector<RiverFall>& steps, double stretch, std::vector<Fall>& falls) {
    std::size_t k = 0;
    while (k < steps.size()) {
        std::size_t last = k;
        while (last + 1 < steps.size() &&
               dmath::hypot(steps[last + 1].x - steps[last].footX,
                            steps[last + 1].z - steps[last].footZ) <= stretch) {
            ++last;
        }
        falls.push_back({steps[k].x, steps[k].z, steps[k].top - steps[last].bottom});
        k = last + 1;
    }
}

} // namespace

void drawCourses(const MapFrame& frame, std::span<const Course> courses, double leastPixels,
                 std::span<MapSample> out) {
    const double least = 0.5 * leastPixels * std::max(frame.stepX(), frame.stepZ());
    for (const Course& c : courses) {
        if (c.xs.size() < 2) {
            continue;
        }
        double ax = frame.offsetX(c.xs[0]);
        double az = frame.offsetZ(c.zs[0]);
        for (std::size_t k = 1; k < c.xs.size(); ++k) {
            const double bx = ax + (c.xs[k] - c.xs[k - 1]);
            const double bz = az + (c.zs[k] - c.zs[k - 1]);
            const Segment s{ax,
                            az,
                            bx,
                            bz,
                            std::max(static_cast<double>(c.halfWidths[k - 1]), least),
                            std::max(static_cast<double>(c.halfWidths[k]), least),
                            static_cast<double>(std::max(c.meanders[k - 1], c.meanders[k]))};
            drawSegment(frame, s, c.discharge, out);
            ax = bx;
            az = bz;
        }
    }
}

void drawFalls(const MapFrame& frame, std::span<const Fall> falls, double centerPixels,
               double ringPixels, double leastDrop, std::span<MapSample> out) {
    const double step = std::max(frame.stepX(), frame.stepZ());
    const double ring = ringPixels * step;
    for (const Fall& fall : falls) {
        if (!(fall.drop >= leastDrop)) {
            continue;
        }
        const double u = frame.offsetX(fall.x);
        const double v = frame.offsetZ(fall.z);
        const PixelRange r = pixelsIn(frame, u - ring, u + ring, v - ring, v + ring);
        for (int j = r.j0; j <= r.j1; ++j) {
            for (int i = r.i0; i <= r.i1; ++i) {
                const double ex = (i + 0.5) * frame.stepX() - u;
                const double ez = (j + 0.5) * frame.stepZ() - v;
                const std::uint8_t mark =
                    markAt(std::sqrt(ex * ex + ez * ez) / step, centerPixels, ringPixels);
                MapSample& s = out[mapGridIndex(frame, i, j)];
                if (mark > s.fallMark || (mark != 0 && mark == s.fallMark && fall.drop > s.fallDrop)) {
                    s.fallMark = mark;
                    s.fallDrop = fall.drop;
                }
            }
        }
    }
}

void reachCourses(const Hydrology& hydrology, const MapFrame& frame, double stretch,
                  std::vector<Course>& courses, std::vector<Fall>& falls, MapSampleStats& stats) {
    const MapBox& b = frame.box();
    const double cx = wrapCoordinate(b.x + 0.5 * b.width, frame.world().x);
    const double cz = wrapCoordinate(b.z + 0.5 * b.height, frame.world().z);
    const double radius = 0.5 * std::sqrt(b.width * b.width + b.height * b.height) + kCourseMargin;
    const std::vector<std::size_t> near = hydrology.reachesNear(cx, cz, radius);
    ++stats.hydrologyCalls;
    for (const std::size_t index : near) {
        const RiverReach reach = hydrology.reach(index);
        ++stats.hydrologyCalls;
        Course c;
        c.discharge = reach.discharge;
        for (const RiverPoint& p : reach.points) {
            c.xs.push_back(p.x);
            c.zs.push_back(p.z);
            c.halfWidths.push_back(0.5f * p.width);
            c.meanders.push_back(p.meander);
        }
        courses.push_back(std::move(c));
        addStretches(reach.falls, stretch, falls);
    }
}

namespace {

// The columns (or rows) of cells over [start, start + size) and one more on each side, all of
// them if that is the whole world.
void cellSpan(double start, double size, double period, double cell, int count, int& first,
              int& last) {
    first = static_cast<int>(std::floor((start + 0.5 * period) / cell)) - 1;
    last = static_cast<int>(std::floor((start + size + 0.5 * period) / cell)) + 1;
    if (last - first + 1 >= count) {
        first = 0;
        last = count - 1;
    }
}

bool inBox(const MapFrame& f, double x, double z, double margin) {
    const double u = f.offsetX(x);
    const double v = f.offsetZ(z);
    return u >= -margin && u <= f.box().width + margin && v >= -margin &&
           v <= f.box().height + margin;
}

} // namespace

void planetCourses(const PlanetLevel& planet, const MapFrame& frame, double minDischarge,
                   double leastDrop, std::vector<Course>& courses, std::vector<Fall>& falls) {
    const PlanetGrid& g = planet.grid();
    const PlanetCells& cells = planet.cells();
    const MapBox& b = frame.box();
    int i0 = 0;
    int i1 = 0;
    int j0 = 0;
    int j1 = 0;
    cellSpan(wrapCoordinate(b.x, g.periodX()), b.width, g.periodX(), g.cellSize, g.nx, i0, i1);
    cellSpan(wrapCoordinate(b.z, g.periodZ()), b.height, g.periodZ(), g.cellSize, g.nz, j0, j1);
    const std::vector<RiverSegment>& segments = planet.rivers().segments;
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const std::size_t cell = g.index(g.wrapColumn(i), g.wrapRow(j));
            if ((cells.bits[cell] & PlanetCells::kRiverBit) == 0) {
                continue;
            }
            const PlanetCells::RiverCell* river = cells.riverAt(cell);
            if (river == nullptr || river->segment < 0 || river->vertex < 0) {
                continue;
            }
            const RiverSegment& s = segments[static_cast<std::size_t>(river->segment)];
            const auto v = static_cast<std::size_t>(river->vertex);
            if (v + 1 >= s.vertices.size() || s.vertices[v].discharge < minDischarge) {
                continue;
            }
            const RiverVertex& a = s.vertices[v];
            const RiverVertex& c = s.vertices[v + 1];
            courses.push_back({{a.x, c.x}, {a.z, c.z}, {0.5f * a.width, 0.5f * c.width},
                               {0.0f, 0.0f}, a.discharge});
        }
    }
    for (const RiverFeature& f : planet.features()) {
        if (f.kind == RiverFeatureKind::Waterfall && f.magnitude >= leastDrop &&
            inBox(frame, f.x, f.z, g.cellSize)) {
            falls.push_back({f.x, f.z, f.magnitude});
        }
    }
}

} // namespace terrain::map_water
