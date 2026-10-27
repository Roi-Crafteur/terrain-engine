// The falls of a course in steps (river_falls.hpp).
#include "river_falls.hpp"

#include "river_slot.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain::water {

namespace {

// The stretches: segments steeper than kStepSlope, the water coming down more than kStepDrop
// meters over them.
constexpr double kStepSlope = 0.12;
constexpr double kStepDrop = 4.0;
// The lips kStepRun meters apart (kRunLeast to kRunLeast + kRunSpread of it, mostly short: a few
// tall drops among small ones), the drops as high as the slope comes down over that run, times
// kHeightLeast to kHeightLeast + kHeightSpread, kStepMin meters at least; a slope steeper than
// kSheer falls at once, kSheerMax meters at most. The face of a drop comes down at kFaceLeastTurn
// to kFaceLeastTurn + kFaceSpreadTurn (70 to 88 degrees) over kFaceLeast meters at least, or as
// steep as the slope where it is steeper. In kDoubleChance of the drops, two: the upper kUpperLeast
// to kUpperLeast + kUpperSpread of it, a ledge kLedgeLeast to kLedgeMost meters long, the rest.
constexpr double kStepRun = 70.0;
constexpr double kRunLeast = 0.25;
constexpr double kRunSpread = 2.75;
constexpr double kHeightLeast = 0.6;
constexpr double kHeightSpread = 0.8;
constexpr double kStepMin = 3.0;
constexpr double kSheer = 1.5;
constexpr double kSheerMax = 400.0;
constexpr double kFaceLeastTurn = 70.0 / 360.0;
constexpr double kFaceSpreadTurn = 18.0 / 360.0;
constexpr double kFaceLeast = 2.0; // meters: a face spans two cells of the finest meshes at least
constexpr double kDoubleChance = 0.25;
constexpr double kUpperLeast = 0.35;
constexpr double kUpperSpread = 0.3;
constexpr double kLedgeLeast = 2.0;
constexpr double kLedgeMost = 5.0;
// The slot of a stretch lies kInciseShare of its mean drop (kInciseLeast to kInciseMost meters)
// deeper below the floor of its valley than its steps alone, at its lips too: its walls never
// vanish between its steps. It fades in and out at the ends of the stretch over kInciseRun times
// that over its least slope, the floor still coming down there (at a half of that at least).
constexpr double kInciseShare = 0.5;
constexpr double kInciseLeast = 2.0;
constexpr double kInciseMost = 20.0;
constexpr double kInciseRun = 3.0;
constexpr std::uint64_t kStepSeed = 0x5ca1ab1e0fa11500ULL;

// A drop: its lip at s along the course, its face down to its foot run meters farther.
struct Drop {
    double s = 0.0;
    double run = 0.0;
    double from = 0.0;
    double to = 0.0;
};

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double slopeOf(const CoursePoint& a, const CoursePoint& b) {
    const double run = b.s - a.s;
    return run > 0.0 ? static_cast<double>(a.level - b.level) / run : 0.0;
}

// The level of the slope of the stretch p[first..last] at s along it.
double levelOn(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last, double s) {
    for (std::size_t i = first; i < last; ++i) {
        if (s <= p[i + 1].s) {
            const double run = p[i + 1].s - p[i].s;
            const double t = run > 0.0 ? std::clamp((s - p[i].s) / run, 0.0, 1.0) : 1.0;
            return lerp(p[i].level, p[i + 1].level, t);
        }
    }
    return p[last].level;
}

// Where the slope of the stretch first comes down to level (it never rises).
double whereOn(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last,
               double level) {
    for (std::size_t i = first; i < last; ++i) {
        const double hi = p[i].level;
        const double lo = p[i + 1].level;
        if (lo <= level) {
            const double t = hi > lo ? std::clamp((hi - level) / (hi - lo), 0.0, 1.0) : 1.0;
            return lerp(p[i].s, p[i + 1].s, t);
        }
    }
    return p[last].s;
}

// Where the slope of the stretch stops being sheer, from s.
double sheerEnd(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last,
                double s) {
    std::size_t i = first;
    while (i < last && p[i + 1].s <= s) {
        ++i;
    }
    double end = s;
    for (; i < last && slopeOf(p[i], p[i + 1]) >= kSheer; ++i) {
        end = p[i + 1].s;
    }
    return end;
}

// A share in [0, 1) from 24 bits of bits, from the bit shift.
double shareOf(std::uint64_t bits, int shift) {
    return static_cast<double>((bits >> shift) & 0xffffffULL) * 0x1.0p-24;
}

// The drop of height from level at s, its face as steep as slope (meters per meter), the next lip
// at next: one drop, or two with a ledge between them where they fit under the slope.
void addDrop(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last, double s,
             double level, double height, double slope, double next, std::uint64_t bits,
             std::vector<Drop>& drops) {
    const double face = std::min(std::max(height / slope, kFaceLeast), next - s);
    const double upper = height * (kUpperLeast + kUpperSpread * shareOf(bits, 24));
    const double ledge = kLedgeLeast + (kLedgeMost - kLedgeLeast) * shareOf(bits, 40);
    const double face1 = std::max(upper / slope, kFaceLeast);
    const double face2 = std::max((height - upper) / slope, kFaceLeast);
    if (shareOf(bits, 0) < kDoubleChance && upper >= kStepMin && height - upper >= kStepMin &&
        s + face1 + ledge + face2 < next &&
        level - upper <= levelOn(p, first, last, s + face1 + ledge)) {
        drops.push_back({s, face1, level, level - upper});
        drops.push_back({s + face1 + ledge, face2, level - upper, level - height});
        return;
    }
    drops.push_back({s, face, level, level - height});
}

// The drops of the stretch p[first..last], from its lip down.
std::vector<Drop> dropsOf(const std::vector<CoursePoint>& p, std::size_t first,
                          std::size_t last) {
    const std::uint64_t seed = noise::deriveSeed(
        noise::deriveSeed(kStepSeed, static_cast<std::uint64_t>(std::llround(p[first].x))),
        static_cast<std::uint64_t>(std::llround(p[first].z)));
    const double bottom = p[last].level;
    double level = p[first].level;
    double s = p[first].s;
    std::vector<Drop> drops;
    for (std::uint64_t k = 0; level > bottom; ++k) {
        const std::uint64_t bits = noise::deriveSeed(seed, k);
        const double u = shareOf(bits, 0);
        const double run = kStepRun * (kRunLeast + kRunSpread * u * u * u);
        double height = (level - levelOn(p, first, last, std::min(s + run, p[last].s))) *
                        (kHeightLeast + kHeightSpread * shareOf(bits, 24));
        const double sheer = level - levelOn(p, first, last, sheerEnd(p, first, last, s));
        if (sheer >= kStepMin) {
            height = sheer;
        }
        height = std::clamp(height, kStepMin, kSheerMax);
        if (level - height - bottom < kStepMin) {
            height = level - bottom;
        }
        const double next = whereOn(p, first, last, level - height);
        const double turn = kFaceLeastTurn + kFaceSpreadTurn * shareOf(bits, 40);
        addDrop(p, first, last, s, level, height, dmath::sinTurns(turn) / dmath::cosTurns(turn),
                next, noise::deriveSeed(bits, 1), drops);
        level -= height;
        s = next;
    }
    return drops;
}

// The level of the stretch in steps at s along it, top above its first drop.
double steppedLevel(const std::vector<Drop>& drops, double top, double s) {
    double level = top;
    for (const Drop& d : drops) {
        if (s <= d.s) {
            return level;
        }
        if (s < d.s + d.run) {
            return lerp(d.from, d.to, (s - d.s) / d.run);
        }
        level = d.to;
    }
    return level;
}

// How much deeper than its steps the slot of the stretch p[first..last] lies (kInciseShare), mean
// the mean drop of its steps: depth meters between its ends from and to, faded over fade meters.
struct Incision {
    double depth = 0.0;
    double fade = 0.0;
    double from = 0.0;
    double to = 0.0;
};

Incision incisionOf(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last,
                    double mean) {
    double least = slopeOf(p[first], p[first + 1]);
    for (std::size_t i = first + 1; i < last; ++i) {
        least = std::min(least, slopeOf(p[i], p[i + 1]));
    }
    Incision c;
    c.from = p[first].s;
    c.to = p[last].s;
    c.depth = std::min(std::clamp(kInciseShare * mean, kInciseLeast, kInciseMost),
                       least * (c.to - c.from) / (2.0 * kInciseRun));
    c.fade = kInciseRun * c.depth / least;
    return c;
}

double incisionAt(const Incision& c, double s) {
    const auto smooth = [](double t) {
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    return c.depth * smooth((s - c.from) / c.fade) * smooth((c.to - s) / c.fade);
}

// A point of segment a-b at s along the course.
CoursePoint pointAt(const CoursePoint& a, const CoursePoint& b, double s, double level) {
    const double run = b.s - a.s;
    const double t = run > 0.0 ? std::clamp((s - a.s) / run, 0.0, 1.0) : 0.0;
    CoursePoint p = a;
    p.x = lerp(a.x, b.x, t);
    p.z = lerp(a.z, b.z, t);
    p.s = s;
    p.level = static_cast<float>(level);
    p.valley = static_cast<float>(lerp(a.valley, b.valley, t));
    p.ground = static_cast<float>(lerp(a.ground, b.ground, t));
    p.top = static_cast<float>(lerp(a.top, b.top, t));
    p.mountain = static_cast<float>(lerp(a.mountain, b.mountain, t));
    p.fall = true;
    return p;
}

// The stretch p[first..last] in steps, appended to out (its last point aside): its points, and
// the lips and feet of its drops between them, with the walls of its slot (slotBase of its mean
// drop) and its valley raised by its incision. Returns the walls.
float appendSteps(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last,
                  std::vector<CoursePoint>& out) {
    constexpr double kNear = 0.05;
    const std::vector<Drop> drops = dropsOf(p, first, last);
    std::vector<double> breaks;
    double mean = 0.0;
    for (const Drop& d : drops) {
        breaks.push_back(d.s);
        breaks.push_back(d.s + d.run);
        mean += (d.from - d.to) / static_cast<double>(drops.size());
    }
    const float slot = slotBase(mean);
    const Incision incision = incisionOf(p, first, last, mean);
    const double top = p[first].level;
    std::size_t b = 0;
    for (std::size_t i = first; i < last; ++i) {
        CoursePoint a = p[i];
        a.level = static_cast<float>(steppedLevel(drops, top, a.s));
        a.valley = static_cast<float>(a.valley + incisionAt(incision, a.s));
        a.fall = true;
        a.slot = slot;
        out.push_back(a);
        for (; b < breaks.size() && breaks[b] < p[i + 1].s - kNear; ++b) {
            if (breaks[b] > out.back().s + kNear) {
                out.push_back(pointAt(p[i], p[i + 1], breaks[b],
                                      steppedLevel(drops, top, breaks[b])));
                out.back().valley = static_cast<float>(out.back().valley +
                                                       incisionAt(incision, breaks[b]));
                out.back().slot = slot;
            }
        }
    }
    return slot;
}

} // namespace

void stepFalls(std::vector<CoursePoint>& points) {
    std::vector<CoursePoint> out;
    out.reserve(points.size());
    bool stepped = false;
    std::size_t i = 0;
    while (i + 1 < points.size()) {
        std::size_t j = i;
        while (j + 1 < points.size() && slopeOf(points[j], points[j + 1]) > kStepSlope) {
            ++j;
        }
        if (j > i && static_cast<double>(points[i].level - points[j].level) > kStepDrop) {
            points[j].slot = appendSteps(points, i, j, out);
            points[j].fall = true; // the foot of the stretch
            stepped = true;
            i = j;
            continue;
        }
        out.push_back(points[i]);
        ++i;
    }
    out.push_back(points.back());
    if (stepped) {
        points = std::move(out);
    }
}

} // namespace terrain::water
