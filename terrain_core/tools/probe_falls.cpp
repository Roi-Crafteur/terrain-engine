// The section falls of terrain_probe (probe_sections.hpp): the falls whose lip lies within --falls
// meters of the point, and the corridor of steps nearest to it: the falls of a reach whose lips
// follow each other within kStepGap meters down the course, from the highest lip to the foot of
// the last one.
#include "probe_sections.hpp"

#include "terrain/det_math.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <tuple>

namespace probe {

namespace {

constexpr double kStepGap = 250.0; // meters along the course between the lips of one corridor

struct Near {
    double distance = 0.0;
    std::size_t reach = 0;
    terrain::RiverFall fall;
};

std::size_t nearestPoint(const terrain::RiverReach& reach, double x, double z) {
    std::size_t best = 0;
    double least = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < reach.points.size(); ++i) {
        const double d = terrain::dmath::hypot(reach.points[i].x - x, reach.points[i].z - z);
        if (d < least) {
            least = d;
            best = i;
        }
    }
    return best;
}

double along(const terrain::RiverReach& reach, std::size_t a, std::size_t b) {
    double meters = 0.0;
    for (std::size_t i = std::min(a, b); i < std::max(a, b); ++i) {
        meters += terrain::dmath::hypot(reach.points[i + 1].x - reach.points[i].x,
                                        reach.points[i + 1].z - reach.points[i].z);
    }
    return meters;
}

// Meters from (x, z) to the course between points a and b, the point taken at its image nearest
// to each segment.
double toCourse(const ProbeWorld& w, const terrain::RiverReach& reach, std::size_t a,
                std::size_t b, double x, double z) {
    double least = std::numeric_limits<double>::infinity();
    for (std::size_t i = std::min(a, b); i <= std::max(a, b); ++i) {
        const terrain::RiverPoint& p = reach.points[i];
        const terrain::RiverPoint& q = reach.points[std::min(i + 1, std::max(a, b))];
        const double px = terrain::nearestImage(x, p.x, w.world().x);
        const double pz = terrain::nearestImage(z, p.z, w.world().z);
        const double sx = q.x - p.x;
        const double sz = q.z - p.z;
        const double length2 = sx * sx + sz * sz;
        const double t =
            length2 > 0.0 ? std::clamp(((px - p.x) * sx + (pz - p.z) * sz) / length2, 0.0, 1.0)
                          : 0.0;
        least = std::min(least, terrain::dmath::hypot(px - p.x - t * sx, pz - p.z - t * sz));
    }
    return least;
}

struct Corridor {
    bool found = false;
    std::size_t reach = 0;
    std::size_t falls = 0;
    double top = 0.0;
    double bottom = 0.0;
    double width = 0.0;
    double length = 0.0;
    double distance = std::numeric_limits<double>::infinity();
};

// The corridors of one reach, the nearest to (x, z) kept in best.
void corridorsOf(const ProbeWorld& w, const terrain::RiverReach& reach, std::size_t r, double x,
                 double z, Corridor& best) {
    std::vector<std::pair<std::size_t, terrain::RiverFall>> lips;
    for (const terrain::RiverFall& f : reach.falls) {
        lips.emplace_back(nearestPoint(reach, f.x, f.z), f);
    }
    std::sort(lips.begin(), lips.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::size_t start = 0;
    while (start < lips.size()) {
        std::size_t end = start;
        while (end + 1 < lips.size() &&
               along(reach, lips[end].first, lips[end + 1].first) <= kStepGap) {
            ++end;
        }
        Corridor c{true, r, end - start + 1};
        c.top = -std::numeric_limits<double>::infinity();
        c.bottom = std::numeric_limits<double>::infinity();
        std::size_t first = reach.points.size();
        std::size_t last = 0;
        for (std::size_t k = start; k <= end; ++k) {
            const terrain::RiverFall& f = lips[k].second;
            c.top = std::max(c.top, static_cast<double>(f.top));
            c.bottom = std::min(c.bottom, static_cast<double>(f.bottom));
            c.width = std::max(c.width, static_cast<double>(f.width));
            const std::size_t foot = nearestPoint(reach, f.footX, f.footZ);
            first = std::min({first, lips[k].first, foot});
            last = std::max({last, lips[k].first, foot});
        }
        c.length = along(reach, first, last);
        c.distance = toCourse(w, reach, first, last, x, z);
        if (std::tie(c.distance, c.reach) < std::tie(best.distance, best.reach)) {
            best = c;
        }
        start = end + 1;
    }
}

Record fallRecord(const ProbeWorld& w, const Near& n) {
    const terrain::RiverFall& f = n.fall;
    Record r;
    r.integer("reach", static_cast<std::int64_t>(n.reach))
        .number("lipX", wrapX(w, f.x), 1, "m")
        .number("lipZ", wrapZ(w, f.z), 1, "m")
        .number("footX", wrapX(w, f.footX), 1, "m")
        .number("footZ", wrapZ(w, f.footZ), 1, "m")
        .number("top", f.top, 2, "m")
        .number("bottom", f.bottom, 2, "m")
        .number("drop", static_cast<double>(f.top) - static_cast<double>(f.bottom), 2, "m")
        .number("width", f.width, 1, "m")
        .number("distance", n.distance, 1, "m");
    return r;
}

Record corridorRecord(const Corridor& c) {
    Record r;
    if (!c.found) {
        for (const char* key : {"reach", "falls", "top", "bottom", "drop", "length", "distance",
                                "inside"}) {
            r.none(key);
        }
        return r;
    }
    r.integer("reach", static_cast<std::int64_t>(c.reach))
        .integer("falls", static_cast<std::int64_t>(c.falls))
        .number("top", c.top, 2, "m")
        .number("bottom", c.bottom, 2, "m")
        .number("drop", c.top - c.bottom, 2, "m")
        .number("length", c.length, 1, "m")
        .number("distance", c.distance, 1, "m")
        .flag("inside", c.distance <= 0.5 * c.width);
    return r;
}

} // namespace

Record fallsSection(const ProbeWorld& w, const Point& p) {
    Record r;
    const terrain::Hydrology* h = w.hydrology();
    const double radius = w.options().falls;
    r.flag("available", h != nullptr).number("radius", radius, 0, "m");
    std::vector<Near> near;
    Corridor corridor;
    if (h != nullptr && radius > 0.0) {
        for (const std::size_t index : h->reachesNear(p.x, p.z, radius)) {
            const terrain::RiverReach reach = h->reach(index);
            for (const terrain::RiverFall& f : reach.falls) {
                const double dx = terrain::nearestImage(f.x, p.x, w.world().x) - p.x;
                const double dz = terrain::nearestImage(f.z, p.z, w.world().z) - p.z;
                const double d = terrain::dmath::hypot(dx, dz);
                if (d <= radius) {
                    near.push_back({d, index, f});
                }
            }
            corridorsOf(w, reach, index, p.x, p.z, corridor);
        }
    }
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) {
        return std::tie(a.distance, a.reach) < std::tie(b.distance, b.reach);
    });
    std::vector<Record> items;
    for (const Near& n : near) {
        items.push_back(fallRecord(w, n));
    }
    if (corridor.found && corridor.distance > radius) {
        corridor = Corridor{};
    }
    return r.list("near", std::move(items)).add("corridor", corridorRecord(corridor));
}

} // namespace probe
