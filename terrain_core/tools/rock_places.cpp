// The places of terrain_export_rock (rock_places.hpp).
#include "rock_places.hpp"

#include "view_places.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace rock_tool {

namespace {

// The K2 scene of mountain_freeze_test: the summit near it.
constexpr double kK2X = 416'857.0;
constexpr double kK2Z = -11'405'966.0;
constexpr double kNear = 120.0;     // meters before the foot of a wall or a fall
constexpr double kNearAbove = 25.0; // meters above the ground of the view of the foot of a wall
constexpr double kEdgeAbove = 60.0; // and of the view along it
constexpr double kSlotDown = 150.0; // the view of the slot: this far down the lip
constexpr double kSlotAbove = 40.0; // and this high above the water
constexpr double kFallSearch = 3'000.0;
constexpr double kFallNear = 80.0; // the view of the last face of a stretch, down its foot
constexpr double kStepGap = 250.0; // meters between the lips of the falls of one stretch
// The sun behind the eye on its right: the faces the eye looks at are lit.
constexpr double kSunBehind = -0.4;
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

// A stretch in steps: the falls of a reach whose lips follow each other within kStepGap meters
// down the course, from its highest lip (point first) to its lowest foot (point last).
struct Stretch {
    std::size_t reach = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    int down = 1; // +1 when the course runs down its points in order, -1 otherwise
    double top = 0.0;
    double bottom = 0.0;
    std::size_t falls = 0;
    bool found = false;
};

// Along-course meters between points a and b of a reach.
double between(const terrain::RiverReach& reach, std::size_t a, std::size_t b) {
    double meters = 0.0;
    for (std::size_t i = std::min(a, b); i < std::max(a, b); ++i) {
        meters += terrain::dmath::hypot(reach.points[i + 1].x - reach.points[i].x,
                                        reach.points[i + 1].z - reach.points[i].z);
    }
    return meters;
}

// The stretches of a reach, each a run of its falls down the course.
std::vector<Stretch> stretchesOf(const terrain::RiverReach& reach, std::size_t r) {
    const int down = reach.points.front().level >= reach.points.back().level ? 1 : -1;
    std::vector<std::pair<std::size_t, terrain::RiverFall>> lips;
    for (const terrain::RiverFall& f : reach.falls) {
        lips.emplace_back(nearestPoint(reach, f.x, f.z), f);
    }
    std::sort(lips.begin(), lips.end(), [down](const auto& a, const auto& b) {
        return down > 0 ? a.first < b.first : a.first > b.first;
    });
    std::vector<Stretch> out;
    for (const auto& [point, f] : lips) {
        if (out.empty() || between(reach, out.back().last, point) > kStepGap) {
            out.push_back({r, point, point, down, f.top, f.bottom, 0, true});
        }
        Stretch& s = out.back();
        s.last = point;
        s.top = std::max(s.top, static_cast<double>(f.top));
        s.bottom = std::min(s.bottom, static_cast<double>(f.bottom));
        ++s.falls;
    }
    // The foot of each stretch: the first point down from its last lip at its lowest water.
    for (Stretch& s : out) {
        std::size_t i = s.last;
        while (static_cast<double>(reach.points[i].level) > s.bottom + 0.5 &&
               (s.down > 0 ? i + 1 < reach.points.size() : i > 0)) {
            i = s.down > 0 ? i + 1 : i - 1;
        }
        s.last = i;
    }
    return out;
}

// The stretch whose lips come nearest to (x, z), within kFallSearch meters.
Stretch stretchNear(const terrain::Hydrology& h, double x, double z) {
    Stretch pick;
    double least = kFallSearch;
    for (const std::size_t r : h.reachesNear(x, z, kFallSearch)) {
        const terrain::RiverReach reach = h.reach(r);
        for (const Stretch& s : stretchesOf(reach, r)) {
            const auto step = static_cast<std::size_t>(1);
            for (std::size_t i = std::min(s.first, s.last); i <= std::max(s.first, s.last);
                 i += step) {
                const double d = terrain::dmath::hypot(reach.points[i].x - x,
                                                       reach.points[i].z - z);
                if (d < least) {
                    least = d;
                    pick = s;
                }
            }
        }
    }
    return pick;
}

// The face of the rock at a column, its middle and how high it stands.
struct FaceSeen {
    double x = 0.0;
    double z = 0.0;
    terrain::RockFace face;
    double score = -1.0;
};

// The tallest face within radius meters of (x, z) whose top juts out (kLeastCap meters at least),
// searched on a lattice of kFaceStep meters: where its middle lies.
FaceSeen tallestFace(const terrain::Heightfield& relief, double x, double z, double radius) {
    constexpr double kFaceStep = 4.0;
    constexpr float kLeastCap = 1.5f;
    std::vector<double> xs;
    std::vector<double> zs;
    for (double dz = -radius; dz <= radius; dz += kFaceStep) {
        for (double dx = -radius; dx <= radius; dx += kFaceStep) {
            xs.push_back(x + dx);
            zs.push_back(z + dz);
        }
    }
    std::vector<terrain::HeightSample> samples(xs.size());
    std::vector<terrain::RockColumn> rock(xs.size());
    relief.samplesAt(xs, zs, samples, rock);
    FaceSeen best;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        for (std::size_t k = 0; k < rock[i].faceCount; ++k) {
            const terrain::RockFace& f = rock[i].faces[k];
            const double score = static_cast<double>(f.top - f.foot) * f.weight;
            if (f.cap >= kLeastCap && score > best.score) {
                best = {xs[i] - f.distance * f.airX, zs[i] - f.distance * f.airZ, f, score};
            }
        }
    }
    return best;
}

// The tallest face with a cap near the foot of the wall, seen from kFaceView meters before it.
void addK2Face(const terrain::Heightfield& relief, const view_places::Point& foot, Places& out) {
    constexpr double kFaceSearch = 400.0;
    constexpr double kFaceView = 50.0;
    const FaceSeen seen = tallestFace(relief, foot.x, foot.z, kFaceSearch);
    if (seen.score < 0.0) {
        return;
    }
    const terrain::RockFace& f = seen.face;
    Shot s = shotAt("k2_face", seen.x + f.airX * kFaceView, seen.z + f.airZ * kFaceView);
    s.heading = view_places::headingTo(s.x, s.z, seen.x, seen.z);
    // Level with the middle of the face: at the eye height of a walker the slope before it hides
    // it.
    const double middle = 0.5 * static_cast<double>(f.foot + f.top);
    s.above = std::max(s.above, middle - static_cast<double>(relief.heightAt(s.x, s.z)));
    s.tilt = 0.0;
    s.height = kTall;
    s.sunLeft = kSunBehind;
    char note[256];
    std::snprintf(note, sizeof note,
                  "the tallest face with a cap within %.0f m of the foot of the wall: %.0f m high "
                  "at (%.0f, %.0f), cap %.1f m, undercut %.1f m, seen from %.0f m level with "
                  "its middle",
                  kFaceSearch, static_cast<double>(f.top - f.foot), seen.x, seen.z,
                  static_cast<double>(f.cap), static_cast<double>(f.undercut), kFaceView);
    s.note = note;
    out.shots.push_back(s);
    out.tops.push_back(topAt("k2_face_top", seen.x, seen.z, s.note));
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
    near.tilt = std::clamp(tiltTo(top - static_cast<double>(foot.height), kNear), 0.0, 0.45);
    near.height = kTall;
    near.sunLeft = kSunBehind;
    near.above = kNearAbove;
    near.note = note + ", seen from 120 m, 25 m above the ground (the slope hides it lower)";
    Shot far = shotAt("k2_wall_far", wall.approach.x, wall.approach.z);
    const double distance = terrain::dmath::hypot(foot.x - far.x, foot.z - far.z);
    far.heading = view_places::headingTo(far.x, far.z, foot.x, foot.z);
    far.tilt = std::clamp(tiltTo(static_cast<double>(foot.height) + 0.5 * wall.height -
                                     static_cast<double>(wall.approach.height),
                                 distance),
                          0.0, 0.45);
    far.sunLeft = kSunBehind;
    far.note = note + ", seen from the end of its approach (view_wall)";
    // Along the wall from 300 m before it, 600 m to its side: it runs from the tiles into the
    // horizon.
    const double offset = 0.75 * reach;
    Shot edge = shotAt("k2_wall_edge", foot.x + away.x * 300.0 - side.x * offset,
                       foot.z + away.z * 300.0 - side.z * offset);
    edge.heading = view_places::headingTo(edge.x, edge.z, foot.x + side.x * 300.0,
                                          foot.z + side.z * 300.0);
    edge.tilt = tiltTo(0.5 * wall.height, offset);
    edge.sunLeft = kSunBehind;
    edge.above = kEdgeAbove;
    edge.note = note + ", along it across the edge of the tiles, 60 m above the ground";
    out.shots.push_back(near);
    out.shots.push_back(far);
    out.shots.push_back(edge);
    out.tops.push_back(
        topAt("k2_wall_top", foot.x - away.x * 100.0, foot.z - away.z * 100.0, note));
    addK2Face(relief, foot, out);
}

bool addFall(const terrain::Hydrology& h, double x, double z, Places& out) {
    const Stretch st = stretchNear(h, x, z);
    if (!st.found) {
        return false;
    }
    const terrain::RiverReach reach = h.reach(st.reach);
    const terrain::RiverPoint& lip = reach.points[st.first];
    const terrain::RiverPoint& foot = reach.points[st.last];
    const double drop = st.top - st.bottom;
    const double length = between(reach, st.first, st.last);
    const std::string note = text("the stretch in steps of %.0f m (%.0f m long) whose highest lip "
                                  "is at (%.0f, ...)",
                                  drop, length, lip.x) +
                             ", " + std::to_string(st.falls) + " falls";
    const double sign = st.down;
    // On its water down the foot, looking up the stretch: whole, then its last face.
    const double far = std::clamp(1.1 * drop, 150.0, 500.0);
    for (const double back : {far, kFallNear}) {
        const std::size_t at = alongCourse(reach, st.last, sign * back);
        const terrain::RiverPoint& p = reach.points[at];
        Shot s = shotAt(back == kFallNear ? "fall_foot_near" : "fall_foot", p.x, p.z);
        const terrain::RiverPoint& aim = back == kFallNear ? foot : lip;
        s.heading = view_places::headingTo(s.x, s.z, aim.x, aim.z);
        const double distance = terrain::dmath::hypot(aim.x - s.x, aim.z - s.z);
        s.tilt = std::clamp(tiltTo(static_cast<double>(back == kFallNear ? foot.level + 30.0f
                                                                          : lip.level) -
                                       static_cast<double>(p.level),
                                   distance),
                            0.0, 0.45);
        s.height = kTall;
        s.sunLeft = kSunBehind;
        s.note = note + (back == kFallNear ? ", from 80 m down its foot" : ", from down its foot");
        out.shots.push_back(s);
    }
    // 40 m over its water 150 m down its highest lip, up and down the slot.
    const std::size_t below = alongCourse(reach, st.first, sign * kSlotDown);
    const terrain::RiverPoint& p = reach.points[below];
    for (const bool up : {true, false}) {
        const terrain::RiverPoint& to = up ? lip : foot;
        Shot slot = shotAt(up ? "fall_slot_up" : "fall_slot_down", p.x, p.z);
        slot.above = kSlotAbove;
        slot.heading = view_places::headingTo(p.x, p.z, to.x, to.z);
        slot.tilt = up ? -0.1 : -0.3;
        slot.sunLeft = kSunBehind;
        slot.note = note + ", its slot from 40 m above its water 150 m down its highest lip";
        out.shots.push_back(slot);
    }
    const std::size_t middle = alongCourse(reach, st.first, sign * 0.5 * length);
    TopShot top = topAt("fall_top", reach.points[middle].x, reach.points[middle].z, note);
    top.side = std::clamp(length + 100.0, 300.0, 600.0);
    out.tops.push_back(top);
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
