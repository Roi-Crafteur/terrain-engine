// The water of the world: the reaches of the planet level and the outlets of its lakes, and their
// courses on demand (hydrology.hpp).
#include "terrain/generation/water/hydrology.hpp"

#include "hydrology_impl.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

using water_detail::kHalfWave;
using water_detail::kStray;

constexpr std::size_t kSlopeSpan = 4;

double distanceToSegment(double x, double z, double ax, double az, double bx, double bz) {
    const double dx = bx - ax;
    const double dz = bz - az;
    const double length2 = dx * dx + dz * dz;
    const double t =
        length2 > 0.0 ? std::clamp(((x - ax) * dx + (z - az) * dz) / length2, 0.0, 1.0) : 0.0;
    return dmath::hypot(x - (ax + t * dx), z - (az + t * dz));
}

double lengthOf(const RiverSegment& s) {
    double length = 0.0;
    for (std::size_t i = 1; i < s.vertices.size(); ++i) {
        length += dmath::hypot(s.vertices[i].x - s.vertices[i - 1].x,
                               s.vertices[i].z - s.vertices[i - 1].z);
    }
    return length;
}

} // namespace

void Hydrology::Impl::index() {
    const PlanetCells& cells = planet->cells();
    rivers.build(grid.cells());
    for (const PlanetCells::RiverCell& rc : cells.riverCells) {
        const RiverSegment& s = planet->rivers().segments[static_cast<std::size_t>(rc.segment)];
        const auto v = static_cast<std::size_t>(rc.vertex);
        if (v + 1 < s.vertices.size() &&
            static_cast<double>(s.vertices[v].discharge) >= params.minDischarge) {
            rivers.set(rc.cell);
        }
    }
    rivers.finish();
    for (std::size_t i = 0; i < cells.riverCells.size(); ++i) {
        if (rivers.has(cells.riverCells[i].cell)) {
            reaches.push_back(static_cast<std::uint32_t>(i));
        }
    }
    lakes.build(grid.cells());
    for (const PlanetCells::LakeCell& lc : cells.lakeCells) {
        if (!planet->lakes()[static_cast<std::size_t>(lc.lake)].playa) {
            lakes.set(lc.cell);
        }
    }
    lakes.finish();
    for (const PlanetCells::LakeCell& lc : cells.lakeCells) {
        if (lakes.has(lc.cell)) {
            lakeOfCell.push_back(lc.lake);
        }
    }
    indexLakes();
    indexOutlets();
    paths.resize(reachCount());
    courses.resize(reachCount());
}

// The outlets: every lake spilling into a river carved, from the floor of the cell of its sill.
void Hydrology::Impl::indexOutlets() {
    const std::vector<RiverSegment>& segments = planet->rivers().segments;
    outletCells.build(grid.cells());
    for (std::size_t si = 0; si < segments.size(); ++si) {
        const RiverSegment& s = segments[si];
        const std::size_t first = s.vertices.empty() ? kNone : reachOfCell(s.vertices[0].cell);
        for (const std::int32_t l : s.sourceLakes) {
            const PlanetLake& lake = planet->lakes()[static_cast<std::size_t>(l)];
            if (first == kNone || lake.pass == planet::kNoCell || outletCells.has(lake.pass)) {
                continue;
            }
            const std::uint8_t floor =
                planet::floorOf(relief, grid, lake.pass, planet->params().floorSamples);
            const std::array<double, 2> p =
                planet::floorPoint(grid, lake.pass, floor, planet->params().floorSamples);
            const RiverVertex& v = s.vertices[0];
            outlets.push_back({lake.pass, l, static_cast<std::int32_t>(si),
                               v.x + wrapCoordinate(p[0] - v.x, grid.periodX()),
                               v.z + wrapCoordinate(p[1] - v.z, grid.periodZ())});
            outletCells.set(lake.pass);
        }
    }
    outletCells.finish();
    std::sort(outlets.begin(), outlets.end(),
              [](const Outlet& a, const Outlet& b) { return a.pass < b.pass; });
    outletOfLake.assign(planet->lakes().size(), -1);
    for (std::size_t k = 0; k < outlets.size(); ++k) {
        outletOfLake[static_cast<std::size_t>(outlets[k].lake)] =
            static_cast<std::int32_t>(reaches.size() + k);
    }
}

water::PathPoint Hydrology::Impl::startOf(std::size_t r) const {
    if (isOutlet(r)) {
        return {outletOf(r).x, outletOf(r).z};
    }
    const RiverVertex& a = segmentOf(r).vertices[static_cast<std::size_t>(cellOf(r).vertex)];
    return {a.x, a.z};
}

water::PathPoint Hydrology::Impl::endOf(std::size_t r) const {
    const auto v = static_cast<std::size_t>(vertexIndex(r) + 1);
    const RiverVertex& b = segmentOf(r).vertices[v];
    return {b.x, b.z};
}

std::size_t Hydrology::Impl::next(std::size_t r) const {
    const RiverSegment& s = segmentOf(r);
    const auto v = static_cast<std::size_t>(vertexIndex(r) + 1); // the end of the reach
    if (v + 1 < s.vertices.size()) {
        return reachOfCell(s.vertices[v].cell);
    }
    if (s.mouth == RiverMouth::Confluence && s.downstream >= 0) {
        const RiverSegment& d = planet->rivers().segments[static_cast<std::size_t>(s.downstream)];
        return reachOfCell(d.vertices.front().cell);
    }
    return kNone;
}

// The reach before: the same segment, or at its start the upstream segment of the largest
// discharge (the main river), else the outlet of the lake it leaves.
std::size_t Hydrology::Impl::previous(std::size_t r) const {
    if (isOutlet(r)) {
        return kNone;
    }
    const PlanetCells::RiverCell& rc = cellOf(r);
    const RiverSegment& s = segmentOf(r);
    if (rc.vertex > 0) {
        return reachOfCell(s.vertices[static_cast<std::size_t>(rc.vertex - 1)].cell);
    }
    std::size_t best = kNone;
    float most = -1.0f;
    for (const std::int32_t u : s.upstream) {
        const RiverSegment& up = planet->rivers().segments[static_cast<std::size_t>(u)];
        if (up.vertices.size() < 2) {
            continue;
        }
        const RiverVertex& last = up.vertices[up.vertices.size() - 2];
        const std::size_t candidate = reachOfCell(last.cell);
        if (candidate != kNone && last.discharge > most) {
            most = last.discharge;
            best = candidate;
        }
    }
    for (const std::int32_t l : s.sourceLakes) {
        if (best == kNone && outletOfLake[static_cast<std::size_t>(l)] >= 0) {
            best = static_cast<std::size_t>(outletOfLake[static_cast<std::size_t>(l)]);
        }
    }
    return best;
}

const Path& Hydrology::Impl::pathOf(std::size_t r) const {
    return paths.get(r, [&] {
        const water::PathPoint a = startOf(r);
        const water::PathPoint b = endOf(r);
        return water::lowestPath(relief, a.x, a.z, b.x, b.z, ladder);
    });
}

const water::Course& Hydrology::Impl::courseOf(std::size_t r) const {
    return courses.get(r, [&] { return makeCourse(r); });
}

// The end of a reach at a point of a segment: the river there, the slope of its levels around
// it, and the phase of its meanders and its distance from the start of the segment.
water::ReachEnd Hydrology::Impl::pointEnd(const RiverSegment& s, std::int32_t segment,
                                          std::int32_t vertex) const {
    const auto v = static_cast<std::size_t>(vertex);
    const RiverVertex& p = s.vertices[v];
    water::ReachEnd e{p.x, p.z, p.elevation, p.discharge, p.width, p.depth, 0.0f, 0.0, 0.0};
    // A start of its own for the noise of each segment.
    e.phase = static_cast<double>(
        noise::deriveSeed(meanderSeed, static_cast<std::uint64_t>(segment)) >> 44);
    for (std::size_t i = 1; i <= v; ++i) {
        const RiverVertex& a = s.vertices[i - 1];
        const RiverVertex& b = s.vertices[i];
        const double run = dmath::hypot(b.x - a.x, b.z - a.z);
        e.distance += run;
        e.phase += run / (kHalfWave * std::max(1.0, 0.5 * static_cast<double>(a.width + b.width)));
    }
    // The slope over kSlopeSpan points on each side: the levels of the cells are rough.
    const std::size_t up = v - std::min(v, kSlopeSpan);
    const std::size_t down = std::min(v + kSlopeSpan, s.vertices.size() - 1);
    double run = 0.0;
    for (std::size_t i = up; i < down; ++i) {
        run += dmath::hypot(s.vertices[i + 1].x - s.vertices[i].x,
                            s.vertices[i + 1].z - s.vertices[i].z);
    }
    if (run > 0.0) {
        e.slope = static_cast<float>(std::max(
            0.0, static_cast<double>(s.vertices[up].elevation - s.vertices[down].elevation) / run));
    }
    return e;
}

water::Course Hydrology::Impl::makeCourse(std::size_t r) const {
    const RiverSegment& s = segmentOf(r);
    water::ReachInput in;
    in.b = pointEnd(s, segmentIndex(r), vertexIndex(r) + 1);
    if (isOutlet(r)) {
        // From the sill of the lake, at its level, to the river it feeds.
        const Outlet& o = outletOf(r);
        in.a = in.b;
        in.a.x = o.x;
        in.a.z = o.z;
        in.a.level = std::max(in.b.level, levelOf(o.lake));
        in.a.distance = -dmath::hypot(in.b.x - o.x, in.b.z - o.z);
    } else {
        in.a = pointEnd(s, segmentIndex(r), vertexIndex(r));
    }
    in.own = &pathOf(r);
    const std::size_t before = previous(r);
    const std::size_t after = next(r);
    in.before = before != kNone ? &pathOf(before) : nullptr;
    in.after = after != kNone ? &pathOf(after) : nullptr;
    if (after != kNone && previous(after) != r) {
        // A tributary: it ends where the main river passes, in the images of this reach.
        const std::size_t main = previous(after);
        const Path& down = pathOf(after);
        water::PathPoint joint =
            main != kNone ? water::smoothedJoint(pathOf(main), down) : down.front();
        joint.x += in.b.x - down.front().x;
        joint.z += in.b.z - down.front().z;
        in.endAt = joint;
    }
    in.segmentLength = lengthOf(s);
    in.taperStart = true;
    in.taperEnd = true;
    return water::buildCourse(relief, in);
}

bool Hydrology::Impl::mayReach(std::size_t r, double x, double z, double radius) const {
    const water::PathPoint a = startOf(r);
    const water::PathPoint b = endOf(r);
    const double px = a.x + noise::wrapCanonical(x - a.x, grid.periodX());
    const double pz = a.z + noise::wrapCanonical(z - a.z, grid.periodZ());
    return distanceToSegment(px, pz, a.x, a.z, b.x, b.z) <= radius + kStray;
}

Hydrology::Impl::CellReaches Hydrology::Impl::reachesOfCell(std::size_t cell) const {
    CellReaches out;
    if (rivers.has(cell)) {
        out.reach[out.count++] = rivers.rankOf(cell);
    }
    if (outletCells.has(cell)) {
        out.reach[out.count++] = reaches.size() + outletCells.rankOf(cell);
    }
    return out;
}

Hydrology::Hydrology(std::uint64_t seed, const Heightfield& relief,
                     std::shared_ptr<const PlanetLevel> planet, const HydrologyParams& params) {
    if (!planet || relief.world().x != planet->grid().periodX() ||
        relief.world().z != planet->grid().periodZ()) {
        throw std::invalid_argument(
            "Hydrology: the relief and the planet level tile different worlds");
    }
    impl_ = std::make_unique<Impl>(seed, relief, std::move(planet), params);
    impl_->index();
}

Hydrology::~Hydrology() = default;

const PlanetLevel& Hydrology::planet() const {
    return *impl_->planet;
}

const Heightfield& Hydrology::relief() const {
    return impl_->relief;
}

const HydrologyParams& Hydrology::params() const {
    return impl_->params;
}

std::size_t Hydrology::reachCount() const {
    return impl_->reachCount();
}

} // namespace terrain
