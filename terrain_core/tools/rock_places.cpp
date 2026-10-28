// The places of terrain_export_rock (rock_places.hpp).
#include "rock_places.hpp"

#include "view_places.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace rock_tool {

namespace {

// The K2 scene of mountain_freeze_test: the summit near it.
constexpr double kK2X = 416'857.0;
constexpr double kK2Z = -11'405'966.0;
constexpr double kNear = 120.0;     // meters before the foot of a wall or a fall
constexpr double kFallFar = 450.0;  // and far enough to see a fall of 388 m whole
constexpr double kSlotDown = 150.0; // the view of the slot: this far down the lip
constexpr double kSlotAbove = 40.0; // and this high above the water
constexpr double kFallSearch = 3'000.0;
constexpr double kGorgeSearch = 40'000.0;
constexpr double kNoFall = 500.0;
constexpr int kTall = 1200; // pixels high of the views that look up a wall, for 1600 wide

std::string text(const char* format, double a, double b, double c) {
    char line[256];
    std::snprintf(line, sizeof line, format, a, b, c);
    return line;
}

Shot shotAt(const char* name, double x, double z) {
    Shot s;
    s.name = name;
    s.x = x;
    s.z = z;
    return s;
}

TopShot topAt(const char* name, double x, double z, const std::string& note) {
    TopShot s;
    s.name = name;
    s.x = x;
    s.z = z;
    s.note = note;
    return s;
}

double tiltTo(double rise, double distance) {
    return std::clamp(0.5 * rise / std::max(distance, 1.0), -0.3, 0.9);
}

struct Direction {
    double x = 0.0;
    double z = -1.0;
};

Direction unit(double dx, double dz, Direction fallback) {
    const double length = terrain::dmath::hypot(dx, dz);
    return length > 1e-9 ? Direction{dx / length, dz / length} : fallback;
}

// The index of the point of the course meters along it from point from (down it when meters > 0),
// the last one reached.
std::size_t alongCourse(const terrain::RiverReach& reach, std::size_t from, double meters) {
    const auto& p = reach.points;
    double walked = 0.0;
    std::size_t i = from;
    while (walked < std::abs(meters)) {
        const std::size_t next = meters > 0.0 ? i + 1 : i - 1;
        if ((meters > 0.0 && next >= p.size()) || (meters < 0.0 && i == 0)) {
            break;
        }
        walked += terrain::dmath::hypot(p[next].x - p[i].x, p[next].z - p[i].z);
        i = next;
    }
    return i;
}

// The direction down the course at point i (over a few points).
Direction downAt(const terrain::RiverReach& reach, std::size_t i) {
    const auto& p = reach.points;
    const std::size_t a = alongCourse(reach, i, -15.0);
    const std::size_t b = alongCourse(reach, i, 15.0);
    return unit(p[b].x - p[a].x, p[b].z - p[a].z, {});
}

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

struct FallPick {
    std::size_t reach = 0;
    terrain::RiverFall fall;
    bool found = false;
};

FallPick highestFall(const terrain::Hydrology& h, double x, double z) {
    FallPick pick;
    for (const std::size_t r : h.reachesNear(x, z, kFallSearch)) {
        const terrain::RiverReach reach = h.reach(r);
        for (const terrain::RiverFall& f : reach.falls) {
            const bool near = terrain::dmath::hypot(f.x - x, f.z - z) <= kFallSearch;
            if (near && (!pick.found || f.top - f.bottom > pick.fall.top - pick.fall.bottom)) {
                pick = {r, f, true};
            }
        }
    }
    return pick;
}

} // namespace

void addK2(const terrain::Heightfield& relief, double reach, Places& out) {
    const view_places::Point summit = view_places::summitNear(relief, kK2X, kK2Z);
    const view_places::Wall wall = view_places::highestWall(relief, summit);
    const view_places::Point& foot = wall.foot;
    // Away from the wall, toward the end of its approach.
    const Direction away = unit(wall.approach.x - foot.x, wall.approach.z - foot.z,
                                {-wall.upX, -wall.upZ});
    const Direction side{-away.z, away.x};
    const double top = static_cast<double>(foot.height) + std::min(wall.height, 3.0 * wall.step);
    const std::string note = text("the highest wall by the K2: foot (%.0f, %.0f), rises %.0f m "
                                  "within 400 m of it",
                                  foot.x, foot.z, wall.height);
    Shot near = shotAt("k2_wall_near", foot.x + away.x * kNear, foot.z + away.z * kNear);
    near.heading = view_places::headingTo(near.x, near.z, foot.x, foot.z);
    near.tilt = tiltTo(top - static_cast<double>(foot.height), kNear);
    near.height = kTall;
    near.note = note + ", seen from 120 m";
    Shot far = shotAt("k2_wall_far", wall.approach.x, wall.approach.z);
    const double distance = terrain::dmath::hypot(foot.x - far.x, foot.z - far.z);
    far.heading = view_places::headingTo(far.x, far.z, foot.x, foot.z);
    far.tilt = std::clamp(tiltTo(static_cast<double>(foot.height) + 0.5 * wall.height -
                                     static_cast<double>(wall.approach.height),
                                 distance),
                          0.0, 0.45);
    far.note = note + ", seen from the end of its approach (view_wall)";
    // Along the wall from 300 m before it, 600 m to its side: it runs from the tiles into the
    // horizon.
    const double offset = 0.75 * reach;
    Shot edge = shotAt("k2_wall_edge", foot.x + away.x * 300.0 - side.x * offset,
                       foot.z + away.z * 300.0 - side.z * offset);
    edge.heading = view_places::headingTo(edge.x, edge.z, foot.x + side.x * 300.0,
                                          foot.z + side.z * 300.0);
    edge.tilt = tiltTo(0.5 * wall.height, offset);
    edge.note = note + ", along it across the edge of the tiles";
    out.shots.push_back(near);
    out.shots.push_back(far);
    out.shots.push_back(edge);
    out.tops.push_back(topAt("k2_wall_top", foot.x - away.x * 100.0, foot.z - away.z * 100.0, note));
}

bool addFall(const terrain::Hydrology& h, double x, double z, Places& out) {
    const FallPick pick = highestFall(h, x, z);
    if (!pick.found) {
        return false;
    }
    const terrain::RiverReach reach = h.reach(pick.reach);
    const terrain::RiverFall& f = pick.fall;
    const double drop = static_cast<double>(f.top - f.bottom);
    const std::size_t lip = nearestPoint(reach, f.x, f.z);
    const Direction down = downAt(reach, alongCourse(reach, lip, 30.0));
    const Direction left{down.z, -down.x};
    const double aside = 0.5 * static_cast<double>(f.width) + 15.0;
    const std::string note =
        text("the fall of %.0f m whose lip is at (%.0f, %.0f)", drop, f.x, f.z);
    for (const double back : {kFallFar, kNear}) {
        Shot s = shotAt(back == kNear ? "fall_foot_near" : "fall_foot",
                        f.x + down.x * back + left.x * aside, f.z + down.z * back + left.z * aside);
        s.heading = view_places::headingTo(s.x, s.z, f.x, f.z);
        s.tilt = back == kNear ? tiltTo(0.35 * drop, back) : tiltTo(0.9 * drop, back);
        s.height = kTall;
        s.note = note + (back == kNear ? ", from 120 m before it" : ", from 450 m before it");
        out.shots.push_back(s);
    }
    const std::size_t below = alongCourse(reach, lip, kSlotDown);
    const terrain::RiverPoint& p = reach.points[below];
    const Direction on = downAt(reach, below);
    Shot slot = shotAt("fall_slot", p.x, p.z);
    slot.above = kSlotAbove;
    slot.heading = view_places::headingTo(p.x, p.z, p.x + on.x, p.z + on.z);
    slot.tilt = -0.3;
    slot.note = note + ", its slot from 40 m above the water 150 m down its lip";
    out.shots.push_back(slot);
    out.tops.push_back(topAt("fall_top", f.x + down.x * 100.0, f.z + down.z * 100.0, note));
    return true;
}

bool addGorge(const terrain::Hydrology& h, double x, double z, Places& out) {
    double best = -1.0;
    terrain::RiverReach chosen;
    std::size_t at = 0;
    for (const std::size_t r : h.reachesNear(x, z, kGorgeSearch)) {
        const terrain::RiverReach reach = h.reach(r);
        if (reach.deepestCut < 100.0f || reach.deepestCut > 300.0f || reach.discharge <= best) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < reach.points.size(); ++i) {
            const terrain::RiverPoint& p = reach.points[i];
            const bool clear = std::none_of(
                reach.falls.begin(), reach.falls.end(), [&](const terrain::RiverFall& f) {
                    return terrain::dmath::hypot(f.x - p.x, f.z - p.z) < kNoFall;
                });
            if (p.gorge >= 0.95f && p.mountain >= 0.5f && clear) {
                best = static_cast<double>(reach.discharge);
                chosen = reach;
                at = i;
                break;
            }
        }
    }
    if (best < 0.0) {
        return false;
    }
    const terrain::RiverPoint& p = chosen.points[at];
    const Direction down = downAt(chosen, at);
    const Direction left{down.z, -down.x};
    const double aside = 0.5 * static_cast<double>(p.width) + 4.0;
    Shot s = shotAt("gorge", p.x + left.x * aside, p.z + left.z * aside);
    s.heading = view_places::headingTo(s.x, s.z, s.x + down.x, s.z + down.z);
    s.tilt = 0.15;
    s.note = text("a gorge cut up to %.0f m, its river of %.0f m3/s, %.0f m wide",
                  static_cast<double>(chosen.deepestCut), best, static_cast<double>(p.width));
    out.shots.push_back(s);
    out.tops.push_back(topAt("gorge_top", p.x, p.z, s.note));
    return true;
}

} // namespace rock_tool
