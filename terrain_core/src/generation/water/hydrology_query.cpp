// The valleys and the lakes at a point, and the courses of the reaches for the renderers
// (hydrology.hpp).
#include "terrain/generation/water/hydrology.hpp"

#include "hydrology_impl.hpp"
#include "river_profile.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace terrain {

namespace {

using water_detail::kSearch;
using water_detail::kStray;
using water_detail::kValleyReach;

// A fall: the water drops more than kFallMin meters along steps steeper than kFallSlope (35
// degrees: a cascade down a steep slope is no fall).
constexpr float kFallMin = 4.0f;
constexpr double kFallSlope = 0.7;
// A river holds the ground in the channel of another one whose water lies more than kHoldAbove
// meters below its own.
constexpr float kHoldAbove = 2.0f;

using water_detail::Candidate;

RiverCarve carveOf(const water::Valley& v) {
    RiverCarve c;
    c.carved = true;
    c.height = v.height;
    c.wet = v.wet;
    c.level = v.level;
    c.floor = v.floorLevel;
    c.gorge = v.gorge;
    c.mountain = v.mountain;
    c.steps = v.steps;
    c.upX = v.upX;
    c.upZ = v.upZ;
    c.slope = v.slope;
    c.above = v.above;
    return c;
}

// The rivers hold the ground out of the channel of the lowest valley (win) of c: beside their
// water (their banks) and in their channels (a lower dry valley never takes the water of a
// river). In the channel of the lowest, only the rivers whose water stands kHoldAbove meters above
// its own: two channels never meet at different levels. A channel holds only below top (the
// relief filled), where its water is. Returns the valley that holds the ground, and sets bank to
// the height of the banks that hold it, if they do.
std::size_t holderOf(const Candidates& c, std::size_t win, double top, double& bank) {
    const water::Valley& low = c.found[win].valley;
    std::size_t holder = win;
    double hold = low.height;
    for (std::size_t k = 0; k < c.count; ++k) {
        const water::Valley& v = c.found[k].valley;
        const double h = v.wet ? v.height : v.bank;
        if (h > hold && (!low.wet || v.level > low.level + kHoldAbove) && (!v.wet || h < top)) {
            hold = h;
            holder = k;
        }
    }
    if (holder == win || c.found[holder].valley.wet) {
        return holder;
    }
    bank = hold;
    return win;
}

} // namespace

bool Hydrology::Impl::adjacent(std::size_t a, std::size_t b) const {
    return next(a) == b || previous(a) == b || next(b) == a || previous(b) == a;
}

void Hydrology::Impl::takeReach(std::size_t r, const water::PathPoint& start,
                                const water::Course& course, const water::BoxCourse* box,
                                double x, double z, double ground, water::WallWarp& warp,
                                Candidates& out) const {
    const double px = start.x + noise::wrapCanonical(x - start.x, grid.periodX());
    const double pz = start.z + noise::wrapCanonical(z - start.z, grid.periodZ());
    for (const water::FineLake& p : course.ponds) {
        if (ground < static_cast<double>(p.level()) && p.level() > out.pond && p.covers(px, pz)) {
            out.pond = p.level();
        }
    }
    const water::Valley v = water::valleyAt(course, px, pz, ground, meanderSeed, warp, box);
    if (v.height < ground + water::kLevee || v.raise > 0.0 ||
        v.bank > -std::numeric_limits<double>::infinity()) {
        out.found[out.count++] = {r, v};
    }
}

Candidates Hydrology::Impl::candidatesAt(double x, double z, double ground) const {
    const std::size_t cell = planetCellAt(grid, x, z);
    const int ci = grid.column(cell);
    const int cj = grid.row(cell);
    Candidates out;
    water::WallWarp warp(wallWarp, x, z, nullptr);
    for (int dj = -kSearch; dj <= kSearch; ++dj) {
        for (int di = -kSearch; di <= kSearch; ++di) {
            const CellReaches here =
                reachesOfCell(grid.index(grid.wrapColumn(ci + di), grid.wrapRow(cj + dj)));
            for (std::size_t k = 0; k < here.count; ++k) {
                const std::size_t r = here.reach[k];
                if (mayReach(r, x, z, kValleyReach)) {
                    takeReach(r, startOf(r), courseOf(r), nullptr, x, z, ground, warp, out);
                }
            }
        }
    }
    return out;
}

const Candidates& Hydrology::Impl::candidatesIn(Region::Data& region, Region::Data::Part& part,
                                                double x, double z, double ground) const {
    Candidates& out = region.candidates;
    out.count = 0;
    out.pond = -std::numeric_limits<float>::infinity();
    water::WallWarp warp(wallWarp, x, z, &region.warp);
    for (Region::Data::Reach& e : part.reaches) {
        if (!e.everywhere && !mayReach(e.reach, x, z, kValleyReach)) {
            continue;
        }
        if (e.course == nullptr) {
            e.course = &courseOf(e.reach);
            e.box = water::boxCourse(*e.course, e.minX, e.minZ, e.maxX, e.maxZ, region.groundMin,
                                     region.groundMax);
        }
        takeReach(e.reach, e.start, *e.course, &e.box, x, z, ground, warp, out);
    }
    return out;
}

RiverCarve Hydrology::riversAt(double x, double z, double ground, Region* region) const {
    const Impl& m = *impl_;
    Region::Data* d = region != nullptr ? region->data_.get() : nullptr;
    Region::Data::Part* part = d != nullptr ? d->partOf(m.grid, x, z, ground) : nullptr;
    return part != nullptr ? m.riversOf(m.candidatesIn(*d, *part, x, z, ground), ground)
                           : m.riversOf(m.candidatesAt(x, z, ground), ground);
}

RiverCarve Hydrology::Impl::riversOf(const Candidates& near, double ground) const {
    const auto& found = near.found;
    const std::size_t count = near.count;
    const float pond = near.pond;
    if (count == 0) {
        RiverCarve out;
        out.pond = pond;
        return out;
    }
    std::size_t win = 0;
    double raise = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const Candidate& c = found[k];
        const Candidate& w = found[win];
        raise = std::max(raise, c.valley.raise);
        if (c.valley.height < w.valley.height ||
            (c.valley.height == w.valley.height && c.reach < w.reach)) {
            win = k;
        }
    }
    double bank = -std::numeric_limits<double>::infinity();
    win = holderOf(near, win, ground + raise, bank);
    RiverCarve out;
    if (std::max(found[win].valley.height, bank) < ground + raise) {
        // The reaches just before and after blend their rock with it; the valleys of other
        // rivers fade it out where they meet it.
        water::Valley v = found[win].valley;
        v.height = std::max(v.height, bank);
        water::ValleyBlend blend;
        for (std::size_t k = 0; k < count; ++k) {
            if (k == win || adjacent(found[k].reach, found[win].reach)) {
                blend.add(found[k].valley);
            } else {
                out.second = std::min(out.second, found[k].valley.height);
            }
        }
        blend.applyTo(v);
        const double second = out.second;
        out = carveOf(v);
        out.second = second;
        out.reach = found[win].reach;
        out.bank = bank;
    }
    out.raise = raise;
    out.pond = pond;
    return out;
}

std::vector<std::size_t> Hydrology::reachesNear(double x, double z, double radius) const {
    const Impl& m = *impl_;
    const std::size_t cell = planetCellAt(m.grid, x, z);
    const int ci = m.grid.column(cell);
    const int cj = m.grid.row(cell);
    const int span = static_cast<int>(std::ceil((radius + kStray) / m.grid.cellSize)) + 2;
    std::vector<std::size_t> out;
    for (int dj = -span; dj <= span; ++dj) {
        for (int di = -span; di <= span; ++di) {
            const Impl::CellReaches here = m.reachesOfCell(
                m.grid.index(m.grid.wrapColumn(ci + di), m.grid.wrapRow(cj + dj)));
            for (std::size_t k = 0; k < here.count; ++k) {
                const std::size_t r = here.reach[k];
                if (m.mayReach(r, x, z, radius)) {
                    out.push_back(r);
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

RiverReach Hydrology::reach(std::size_t index) const {
    const Impl& m = *impl_;
    const water::Course& c = m.courseOf(index);
    RiverReach out;
    out.segment = m.segmentIndex(index);
    out.vertex = m.vertexIndex(index);
    out.discharge =
        m.segmentOf(index).vertices[static_cast<std::size_t>(std::max(0, out.vertex))].discharge;
    out.deepestCut = c.deepestCut;
    // Along its water: where its slots wind, on their middle (water::Course::line).
    const bool wound = !c.line.empty();
    const std::vector<water::CoursePoint>& line = wound ? c.line : c.points;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const water::CoursePoint& p = line[i];
        out.points.push_back({p.x, p.z, p.level, 2.0f * p.half, p.depth, 2.0f * p.floorHalf,
                              p.gorge, p.mountain, p.meander, p.valley, p.shift,
                              wound ? c.lineAxis[i].x : p.x, wound ? c.lineAxis[i].z : p.z});
    }
    // A point of the course on the middle of its slot (the lip or the foot of a fall).
    const auto onSlot = [&c](std::size_t i) {
        const water::CoursePoint& a = c.points[i];
        const water::CoursePoint& b = c.points[std::min(i + 1, c.points.size() - 1)];
        const double length = dmath::hypot(b.x - a.x, b.z - a.z);
        const double shift = length > 0.0 ? static_cast<double>(a.shift) / length : 0.0;
        return std::array<double, 2>{a.x - shift * (b.z - a.z), a.z + shift * (b.x - a.x)};
    };
    for (std::size_t i = 0; i + 1 < c.points.size();) {
        std::size_t j = i;
        while (j + 1 < c.points.size() &&
               static_cast<double>(c.points[j].level - c.points[j + 1].level) >
                   kFallSlope * (c.points[j + 1].s - c.points[j].s)) {
            ++j;
        }
        if (j > i && c.points[i].level - c.points[j].level > kFallMin) {
            const std::array<double, 2> lip = onSlot(i);
            const std::array<double, 2> foot = onSlot(j);
            out.falls.push_back({lip[0], lip[1], c.points[i].level, c.points[j].level,
                                 2.0f * c.points[i].half, foot[0], foot[1]});
        }
        i = j > i ? j : i + 1;
    }
    for (const water::FallBlock& b : c.blocks) {
        out.blocks.push_back({b.x, b.z, b.radius, b.top});
    }
    return out;
}

std::size_t Hydrology::coursesComputed() const {
    return impl_->courses.count();
}

std::size_t Hydrology::courseBytes() const {
    return impl_->courses.bytes([](const water::Course& c) {
        return sizeof(c) +
               (c.points.capacity() + c.line.capacity()) * sizeof(water::CoursePoint) +
               c.lineAxis.capacity() * sizeof(water::PathPoint) +
               c.windings.capacity() * sizeof(water::SlotWinding) +
               c.pools.capacity() * sizeof(water::FallPool) +
               (c.rises.capacity() + c.valleyRises.capacity()) * sizeof(float) +
               c.chunks.capacity() * sizeof(water::CourseChunk);
    });
}

} // namespace terrain
