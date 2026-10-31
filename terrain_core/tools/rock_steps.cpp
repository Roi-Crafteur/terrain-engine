// The eyes on the steps of a stretch of falls (rock_steps.hpp).
#include "rock_steps.hpp"

#include "view_places.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace rock_tool {

namespace {

constexpr double kEye = 1.8;       // meters above the water
constexpr float kLevelNear = 0.5f; // an eye over the water of its pool: within this of its level
constexpr double kLipLook = 30.0;  // and looking at the course this far down the lip
constexpr double kFootOut = 12.0;  // the eye at a foot: this far down the foot of its face
constexpr double kFootShare = 0.3; // at most this share of the way to the next lip
constexpr double kPoolMost = 60.0; // the eye in a basin: halfway to the next lip, at most this far
constexpr float kFootWater = 0.5f; // the foot: the first point within this of the water below
constexpr double kSunBehind = -0.4;

struct Step {
    std::size_t lip = 0;  // the point of its lip
    std::size_t foot = 0; // the first point down it at its water below
    std::size_t next = 0; // the lip of the next fall down the course, or the foot of the stretch
    terrain::RiverFall fall;
};

std::size_t downOne(const Stretch& st, std::size_t i) {
    return st.down > 0 ? i + 1 : i - 1;
}

// Whether point p of a course lies over water within kLevelNear of level (not on a block, not
// over a face).
bool onWater(const terrain::Heightfield& field, const terrain::RiverPoint& p, float level) {
    const terrain::HeightSample h = field.sampleAt(p.x, p.z);
    return h.water != terrain::WaterKind::None && h.waterLevel > h.height &&
           std::abs(h.waterLevel - level) < kLevelNear;
}

// The point nearest to point at, from a to b (either way), over water at level; at without one.
std::size_t wetNear(const terrain::Heightfield& field, const terrain::RiverReach& reach,
                    std::size_t at, std::size_t a, std::size_t b, float level) {
    const std::size_t lo = std::min(a, b);
    const std::size_t hi = std::max(a, b);
    for (std::size_t d = 0; d <= hi - lo; ++d) {
        if (at >= lo + d && onWater(field, reach.points[at - d], level)) {
            return at - d;
        }
        if (at + d <= hi && onWater(field, reach.points[at + d], level)) {
            return at + d;
        }
    }
    return at;
}

// The falls of the stretch dropping least meters or more, down the course.
std::vector<Step> stepsOf(const terrain::RiverReach& reach, const Stretch& st, double least) {
    const std::size_t lo = std::min(st.first, st.last);
    const std::size_t hi = std::max(st.first, st.last);
    std::vector<Step> all;
    for (const terrain::RiverFall& f : reach.falls) {
        const std::size_t at = nearestPoint(reach, f.x, f.z);
        if (at >= lo && at <= hi) {
            all.push_back({at, at, st.last, f});
        }
    }
    std::sort(all.begin(), all.end(), [&st](const Step& a, const Step& b) {
        return st.down > 0 ? a.lip < b.lip : a.lip > b.lip;
    });
    std::vector<Step> out;
    for (std::size_t k = 0; k < all.size(); ++k) {
        Step s = all[k];
        s.next = k + 1 < all.size() ? all[k + 1].lip : st.last;
        while (s.foot != s.next && reach.points[s.foot].level > s.fall.bottom + kFootWater) {
            s.foot = downOne(st, s.foot);
        }
        if (static_cast<double>(s.fall.top - s.fall.bottom) >= least) {
            out.push_back(s);
        }
    }
    return out;
}

double degrees(double turns) {
    return std::fmod(360.0 * turns + 720.0, 360.0);
}

// An eye at point at of a reach, looking at point aim, along meters down the lip of its step.
struct Eye {
    const char* what = "";
    std::size_t at = 0;
    std::size_t aim = 0;
    double tilt = 0.0;
    double along = 0.0;
};

// The line of an eye, and its view in out.
std::string eyeAt(const terrain::Heightfield& field, const terrain::RiverReach& reach, const Eye& e,
                  const std::string& name, Places& out) {
    const terrain::RiverPoint& p = reach.points[e.at];
    const terrain::RiverPoint& a = reach.points[e.aim];
    const terrain::HeightSample h = field.sampleAt(p.x, p.z);
    const bool wet = h.water != terrain::WaterKind::None && h.waterLevel > h.height;
    Shot s;
    s.name = name;
    s.x = p.x;
    s.z = p.z;
    s.above = kEye;
    s.heading = view_places::headingTo(p.x, p.z, a.x, a.z);
    s.tilt = e.tilt;
    s.sunLeft = kSunBehind;
    s.note = std::string("the ") + e.what + " of a step of the fall of 388 m";
    out.shots.push_back(s);
    const double level = static_cast<double>(wet ? h.waterLevel : h.height);
    char line[320];
    std::snprintf(line, sizeof line,
                  "  %-5s (%.1f, %.1f), %.0f m down the lip: water %.1f m, eye %.1f m, heading "
                  "%.1f degrees, tilt %.2f%s; --at %.1f %.1f %.1f %.2f %.1f\n",
                  e.what, p.x, p.z, e.along, level, level + kEye, degrees(s.heading), e.tilt,
                  wet ? "" : " (DRY: over the ground)", p.x, p.z, degrees(s.heading), e.tilt, kEye);
    return line;
}

// The three eyes of step s of stretch st, the k-th: in its basin and at its foot (in its pool, off
// its blocks), on its lip (on the water above it, up the course from the lip cut back into it).
std::string eyesOf(const terrain::Heightfield& field, const terrain::RiverReach& reach,
                   const Stretch& st, const Step& s, std::size_t k, Places& out) {
    const double sign = st.down;
    const double drop = static_cast<double>(s.fall.top - s.fall.bottom);
    const double pool = between(reach, s.foot, s.next);
    const auto inPool = [&](double meters) {
        return wetNear(field, reach, alongCourse(reach, s.foot, sign * meters), s.foot, s.next,
                       s.fall.bottom);
    };
    const std::size_t basin = inPool(std::min(kPoolMost, 0.5 * pool));
    const std::size_t foot = inPool(std::min(kFootOut, kFootShare * pool));
    const std::size_t lip = wetNear(field, reach, s.lip, s.lip,
                                    alongCourse(reach, s.lip, -sign * kPoolMost), s.fall.top);
    const auto up = [&](std::size_t at, double most) {
        const terrain::RiverPoint& p = reach.points[at];
        const terrain::RiverPoint& l = reach.points[s.lip];
        const double away = terrain::dmath::hypot(p.x - l.x, p.z - l.z);
        return std::clamp(0.5 * drop / std::max(away, 5.0), 0.05, most);
    };
    const double down =
        std::clamp(0.5 * drop / std::max(between(reach, s.lip, s.foot), 5.0), 0.1, 0.5);
    const auto along = [&](std::size_t at) {
        return (st.down > 0) == (at >= s.lip) ? between(reach, s.lip, at)
                                              : -between(reach, s.lip, at);
    };
    const std::size_t ahead = alongCourse(reach, s.lip, sign * kLipLook);
    const std::string name = "step" + std::to_string(k + 1);
    std::string text = eyeAt(field, reach, {"basin", basin, s.lip, up(basin, 0.6), along(basin)},
                             name + "_basin", out);
    text += eyeAt(field, reach, {"lip", lip, ahead, -down, along(lip)}, name + "_lip", out);
    text += eyeAt(field, reach, {"foot", foot, s.lip, up(foot, 0.9), along(foot)}, name + "_foot",
                  out);
    return text;
}

} // namespace

std::string addSteps(const terrain::Heightfield& field, const terrain::Hydrology& hydrology,
                     double x, double z, double least, Places& out) {
    const Stretch st = stretchNear(hydrology, x, z);
    if (!st.found) {
        return "no stretch in steps near the fall of 388 m\n";
    }
    const terrain::RiverReach reach = hydrology.reach(st.reach);
    const std::vector<Step> steps = stepsOf(reach, st, least);
    std::string text;
    char line[320];
    std::snprintf(line, sizeof line,
                  "the steps of %.0f m or more of the stretch of %.0f m (water %.1f to %.1f m): "
                  "%zu of its %zu falls; eyes %.1f m above the water\n",
                  least, st.top - st.bottom, st.top, st.bottom, steps.size(), st.falls, kEye);
    text += line;
    for (std::size_t k = 0; k < steps.size(); ++k) {
        const terrain::RiverFall& f = steps[k].fall;
        std::snprintf(line, sizeof line,
                      "step %zu: drop %.1f m, water %.1f to %.1f m (%.1f to %.1f m from the top "
                      "of the stretch), lip at (%.1f, %.1f)\n",
                      k + 1, static_cast<double>(f.top - f.bottom), static_cast<double>(f.top),
                      static_cast<double>(f.bottom), f.top - st.top, f.bottom - st.top, f.x, f.z);
        text += line;
        text += eyesOf(field, reach, st, steps[k], k, out);
    }
    return text;
}

} // namespace rock_tool
