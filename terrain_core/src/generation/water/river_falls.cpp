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
// steep as the slope where it is steeper. In kCascadeChance of the drops where the slope is not
// sheer, rapids instead: a ramp down as far as kCascadeLeast to kCascadeMost drops of kStepLeast to
// kStepLeast + kStepSpread meters each would come, kRampLeast to kRampLeast + kRampSpread meters
// per meter (drawn), steeper where the slope is (its water never above the slope, looked at every
// kRampProbe meters), eased at its ends (kRampEase); steeper than kRampMost, a single drop
// instead. No pool between two drops is shorter than kPoolLeast meters, nor than kPoolWidths times
// the width of the channel: closer, the two are one drop (a box seen from above otherwise, its lips
// without room to bend).
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
constexpr double kCascadeChance = 0.2;
constexpr int kCascadeLeast = 2;
constexpr int kCascadeMost = 4;
constexpr double kStepLeast = 2.0;
constexpr double kStepSpread = 4.0;
constexpr double kRampLeast = 0.3;
constexpr double kRampSpread = 0.2;
constexpr double kRampMost = 0.6;
constexpr double kRampProbe = 1.0;
// The slope of rapids grows over kRampEase meters about their top and fades over as many about
// their foot (their water has no kink), round about where it breaks (kEaseBreaks points of the
// course on each), half on the ramp and half in the pool or the river beside, half of it at most:
// their water dips under the line of the pool above and of the ramp (so under the slope), and
// comes ramp times kRampEase / 8 meters at most above the line of the pool below.
constexpr double kRampEase = 4.0;
constexpr int kEaseBreaks = 4;
constexpr double kPoolLeast = 8.0;
constexpr double kPoolWidths = 0.4;
// The slot of a stretch lies kInciseShare of its mean drop (kInciseLeast to kInciseMost meters)
// deeper below the floor of its valley than its steps alone, at its lips too: its walls never
// vanish between its steps. Its steps come down under the slope less that (the floor of the
// valley stays on the slope, never above the relief there). It fades in and out at the ends of the
// stretch over kInciseRun times that over its least slope: the line the steps come down under
// still comes down there (at a half of the least slope at least). That line is drawn every
// kUnderStep meters at most.
constexpr double kInciseShare = 0.5;
constexpr double kInciseLeast = 2.0;
constexpr double kInciseMost = 20.0;
constexpr double kInciseRun = 3.0;
constexpr double kUnderStep = 4.0;
constexpr std::uint64_t kStepSeed = 0x5ca1ab1e0fa11500ULL;

// A drop: its lip at s along the course, its face down to its foot run meters farther.
struct Drop {
    double s = 0.0;
    double run = 0.0;
    double from = 0.0;
    double to = 0.0;
    bool rapids = false;
    // Rapids: the meters about s and about s + run over which their slope grows from that of the
    // water above (above, meters per meter) and fades.
    double easeTop = 0.0;
    double easeFoot = 0.0;
    double above = 0.0;
};

// The water above a stretch in steps: the meters of the course before it and their slope.
struct Above {
    double room = 0.0;
    double slope = 0.0;
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

// Rapids from their top at s down from level, a face as steep as slope where they fall at once
// instead: their drop appended. Returns where they end and their water.
struct Stair {
    double s = 0.0;
    double level = 0.0;
};

Stair addRapids(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last, double s,
                double level, double slope, std::uint64_t bits, std::vector<Drop>& drops) {
    const double bottom = p[last].level;
    const int count =
        kCascadeLeast +
        std::min(kCascadeMost - kCascadeLeast,
                 static_cast<int>(shareOf(bits, 0) * (kCascadeMost - kCascadeLeast + 1)));
    double height = 0.0;
    for (int n = 0; n < count; ++n) {
        const std::uint64_t draw = noise::deriveSeed(bits, static_cast<std::uint64_t>(n));
        height += kStepLeast + kStepSpread * shareOf(draw, 0);
    }
    if (level - height - bottom < kStepLeast) {
        height = level - bottom;
    }
    // Under the slope all along: as steep as the slope comes down from its top at least.
    double ramp = kRampLeast + kRampSpread * shareOf(bits, 40);
    const double reach = height / ramp;
    for (double u = kRampProbe; u <= reach; u += kRampProbe) {
        ramp = std::max(ramp, (level - levelOn(p, first, last, s + u)) / u);
    }
    const double run = ramp > kRampMost ? std::max(height / slope, kFaceLeast) : height / ramp;
    drops.push_back({s, run, level, level - height, !(ramp > kRampMost)});
    return {s + run, level - height};
}

// The half width of the channel of the stretch p[first..last] at s along it.
double halfOn(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last, double s) {
    for (std::size_t i = first; i < last; ++i) {
        if (s <= p[i + 1].s) {
            const double run = p[i + 1].s - p[i].s;
            const double t = run > 0.0 ? std::clamp((s - p[i].s) / run, 0.0, 1.0) : 1.0;
            return lerp(p[i].half, p[i + 1].half, t);
        }
    }
    return p[last].half;
}

// The drops with no short pool between two faces (kPoolLeast, kPoolWidths): closer, one drop,
// its face as steep as the gentler of theirs (never a ramp as steep as a fall), from the lip of
// the first. Rapids are never joined: they have no lip to bend.
std::vector<Drop> mergeClose(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last,
                             const std::vector<Drop>& drops) {
    const auto slopeOfDrop = [](const Drop& d) { return (d.from - d.to) / d.run; };
    std::vector<Drop> out;
    for (const Drop& d : drops) {
        if (!out.empty()) {
            Drop& above = out.back();
            const double least =
                std::max(kPoolLeast, kPoolWidths * 2.0 * halfOn(p, first, last, d.s));
            const double steep = std::min(slopeOfDrop(above), slopeOfDrop(d));
            if (d.s - (above.s + above.run) < least && steep > kRampMost) {
                above.run = std::min(d.s + d.run - above.s,
                                     std::max((above.from - d.to) / steep, kFaceLeast));
                above.to = d.to;
                continue;
            }
        }
        out.push_back(d);
    }
    return out;
}

// The rapids among drops eased at their ends (kRampEase), the stretch from first to last along
// the course with above before it: each takes half of the pool or the river beside it at most.
std::vector<Drop> easeRapids(std::vector<Drop> drops, double first, double last,
                             const Above& above) {
    double end = first - above.room; // where the water above the drop starts to be free
    for (std::size_t k = 0; k < drops.size(); ++k) {
        Drop& d = drops[k];
        const double below = (k + 1 < drops.size() ? drops[k + 1].s : last) - (d.s + d.run);
        if (d.rapids && d.run > 0.0) {
            d.easeTop = std::clamp(d.s - end, 0.0, std::min(kRampEase, d.run));
            d.easeFoot = std::clamp(below, 0.0, std::min(kRampEase, d.run));
            d.above = k == 0 && d.s <= first ? above.slope : 0.0;
        }
        end = d.s + d.run + 0.5 * d.easeFoot;
    }
    return drops;
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
        const double turn = kFaceLeastTurn + kFaceSpreadTurn * shareOf(bits, 40);
        const double steep = dmath::sinTurns(turn) / dmath::cosTurns(turn);
        const double sheer = level - levelOn(p, first, last, sheerEnd(p, first, last, s));
        if (!(sheer >= kStepMin) && shareOf(noise::deriveSeed(bits, 2), 0) < kCascadeChance) {
            const Stair after =
                addRapids(p, first, last, s, level, steep, noise::deriveSeed(bits, 3), drops);
            level = after.level;
            s = std::max(after.s, whereOn(p, first, last, level));
            continue;
        }
        const double u = shareOf(bits, 0);
        const double run = kStepRun * (kRunLeast + kRunSpread * u * u * u);
        double height = (level - levelOn(p, first, last, std::min(s + run, p[last].s))) *
                        (kHeightLeast + kHeightSpread * shareOf(bits, 24));
        if (sheer >= kStepMin) {
            height = sheer;
        }
        height = std::clamp(height, kStepMin, kSheerMax);
        if (level - height - bottom < kStepMin) {
            height = level - bottom;
        }
        const double next = whereOn(p, first, last, level - height);
        const double face = std::min(std::max(height / steep, kFaceLeast), next - s);
        drops.push_back({s, face, level, level - height});
        level -= height;
        s = next;
    }
    return mergeClose(p, first, last, drops);
}

// The level of eased rapids d at s along the course, within kRampEase / 2 of its top or foot or
// between them: from the line of the water above to that of the ramp and to the pool below, round.
double easedLevel(const Drop& d, double s) {
    const double ramp = (d.from - d.to) / d.run;
    const double x = s - d.s;
    const double top = 0.5 * d.easeTop;
    const double foot = 0.5 * d.easeFoot;
    if (x < top) {
        const double in = x + top;
        return d.from - d.above * x -
               (ramp - d.above) * in * in / (2.0 * std::max(d.easeTop, 1e-9));
    }
    const double past = x - d.run;
    if (past > -foot) {
        const double in = past + foot;
        return d.to - ramp * past + ramp * in * in / (2.0 * std::max(d.easeFoot, 1e-9));
    }
    return d.from - ramp * x;
}

// The level of the stretch in steps at s along it, top above its first drop.
double steppedLevel(const std::vector<Drop>& drops, double top, double s) {
    double level = top;
    for (const Drop& d : drops) {
        const bool eased = d.easeTop > 0.0 || d.easeFoot > 0.0;
        if (eased ? s < d.s - 0.5 * d.easeTop : s <= d.s) {
            return level;
        }
        if (s < d.s + d.run + 0.5 * d.easeFoot) {
            return eased ? easedLevel(d, s) : lerp(d.from, d.to, (s - d.s) / d.run);
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

// The mean drop of drops.
double meanOf(const std::vector<Drop>& drops) {
    double mean = 0.0;
    for (const Drop& d : drops) {
        mean += (d.from - d.to) / static_cast<double>(drops.size());
    }
    return mean;
}

// The line the steps of the stretch p[first..last] come down under: its slope less its incision
// cut, every kUnderStep meters at most (only where its points lie along the course and their
// levels).
std::vector<CoursePoint> slopeUnder(const std::vector<CoursePoint>& p, std::size_t first,
                                    std::size_t last, const Incision& cut) {
    std::vector<CoursePoint> out;
    for (std::size_t i = first; i <= last; ++i) {
        if (i > first) {
            const CoursePoint& a = p[i - 1];
            const double run = p[i].s - a.s;
            const int pieces = std::max(1, static_cast<int>(std::ceil(run / kUnderStep)));
            for (int k = 1; k < pieces; ++k) {
                const double t = static_cast<double>(k) / pieces;
                out.push_back(pointAt(a, p[i], a.s + run * t, lerp(a.level, p[i].level, t)));
            }
        }
        out.push_back(p[i]);
    }
    for (CoursePoint& q : out) {
        q.level = static_cast<float>(q.level - incisionAt(cut, q.s));
    }
    return out;
}

// The stretch p[first..last] in steps, appended to out (its last point aside): its points, and
// the lips and feet of its drops between them, with the walls of its slot (slotBase of its mean
// drop). Its steps come down under its slope less its incision, its valley on the slope. Returns
// the walls.
float appendSteps(const std::vector<CoursePoint>& p, std::size_t first, std::size_t last,
                  std::vector<CoursePoint>& out) {
    constexpr double kNear = 0.05;
    const Incision incision = incisionOf(p, first, last, meanOf(dropsOf(p, first, last)));
    const std::vector<CoursePoint> under = slopeUnder(p, first, last, incision);
    const Above above = first > 0 ? Above{p[first].s - p[first - 1].s,
                                          slopeOf(p[first - 1], p[first])}
                                  : Above{};
    const std::vector<Drop> drops =
        easeRapids(dropsOf(under, 0, under.size() - 1), p[first].s, p[last].s, above);
    std::vector<double> breaks;
    for (const Drop& d : drops) {
        const double foot = d.s + d.run;
        for (int k = 0; k <= kEaseBreaks && d.easeTop > 0.0; ++k) {
            breaks.push_back(d.s + d.easeTop * (static_cast<double>(k) / kEaseBreaks - 0.5));
        }
        breaks.push_back(d.s);
        breaks.push_back(foot);
        for (int k = 0; k <= kEaseBreaks && d.easeFoot > 0.0; ++k) {
            breaks.push_back(foot + d.easeFoot * (static_cast<double>(k) / kEaseBreaks - 0.5));
        }
    }
    std::sort(breaks.begin(), breaks.end());
    const float slot = slotBase(meanOf(drops));
    const double top = p[first].level;
    std::size_t b = 0;
    // The top of rapids at the top of the stretch eased in the river above it (its points aside).
    for (; b < breaks.size() && breaks[b] < p[first].s - kNear; ++b) {
        if (first > 0 && breaks[b] > out.back().s + kNear) {
            out.push_back(pointAt(p[first - 1], p[first], breaks[b],
                                  steppedLevel(drops, top, breaks[b])));
            out.back().fall = p[first - 1].fall;
        }
    }
    for (std::size_t i = first; i < last; ++i) {
        CoursePoint a = p[i];
        a.level = static_cast<float>(steppedLevel(drops, top, a.s));
        a.fall = true;
        a.slot = slot;
        out.push_back(a);
        for (; b < breaks.size() && breaks[b] < p[i + 1].s - kNear; ++b) {
            if (breaks[b] > out.back().s + kNear) {
                out.push_back(pointAt(p[i], p[i + 1], breaks[b],
                                      steppedLevel(drops, top, breaks[b])));
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
