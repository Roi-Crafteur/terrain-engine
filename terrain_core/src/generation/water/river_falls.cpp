// The falls of a course in steps (river_falls.hpp).
#include "river_falls.hpp"

#include "river_profile.hpp"
#include "river_slot.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
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
// The winding of a stretch sampled where the water falls (windFalls): two gradient noises over
// waves of kWindWave to kWindWave + kWindSpread meters, then kWindNext to kWindNext +
// kWindNextSpread of the first (lengths of no common measure: no bend comes back at a regular
// interval), the waves longer in proportion for slots wider than kWindRef meters (a half width),
// kWindLeast meters at least; each moves the axis by up to kWindShare of the half width of the
// slot, from 1 - kWindDraw / 2 to 1 + kWindDraw / 2 of it, both within bends of a radius of
// kWindRadius of that half width (kChannelRadius of its channel at least), a noise of amplitude a
// over waves of w bending by kNoiseBend a (2 / w)^2 at most (nearly: its 95th percentile). The
// winding comes back to the axis over kWindFade of the longest wave at each end of the stretch,
// a quarter of it at most.
constexpr double kWindWave = 60.0;
constexpr double kWindSpread = 40.0;
constexpr double kWindNext = 0.5;
constexpr double kWindNextSpread = 0.15;
constexpr double kWindRef = 14.0;
constexpr double kWindLeast = 25.0;
constexpr std::array<double, 2> kWindShare = {1.0, 0.25};
constexpr double kWindDraw = 0.6;
constexpr double kWindRadius = 0.4;
constexpr double kChannelRadius = 0.6;
constexpr double kNoiseBend = 10.0;
constexpr double kWindFade = 0.5;
// Within kFaceStraight of that half width of each face of its falls, the axis keeps its course (a
// straight line where the water falls), its winding back in full from four times as far: the face
// and the pools around it in line, the ground beside the pool below never cutting across the
// face, the bends there no sharper than elsewhere. The half width of the slot is read from the
// mean drop of its steps, about kMeanRun meters of its slope (dropsOf).
constexpr double kFaceStraight = 0.5;
constexpr double kFaceSlope = 1.0; // a face: the water coming down more than this per meter
constexpr double kMeanRun = 65.6;
// The run is sampled every kWindStep meters at most where it winds most: a segment is cut in as
// many as bring the middle of the winding within kWindChord meters of its chord.
constexpr double kWindStep = 4.0;
constexpr double kWindChord = 0.25;
constexpr std::uint64_t kWindSeed = 0x77696e6473313241ULL;

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

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A point of segment a-b at s along the course: all of it as a, but where it lies, its levels and
// its relief, interpolated; on a stretch in steps where both are.
CoursePoint between(const CoursePoint& a, const CoursePoint& b, double s) {
    const double t = b.s > a.s ? std::clamp((s - a.s) / (b.s - a.s), 0.0, 1.0) : 0.0;
    CoursePoint p = a;
    p.x = lerp(a.x, b.x, t);
    p.z = lerp(a.z, b.z, t);
    p.s = s;
    p.level = static_cast<float>(lerp(a.level, b.level, t));
    p.valley = static_cast<float>(lerp(a.valley, b.valley, t));
    p.ground = static_cast<float>(lerp(a.ground, b.ground, t));
    p.top = static_cast<float>(lerp(a.top, b.top, t));
    p.mountain = static_cast<float>(lerp(a.mountain, b.mountain, t));
    p.fall = a.fall && b.fall;
    p.slot = p.fall ? std::max(a.slot, b.slot) : 0.0f;
    return p;
}

// How far the axis of a stretch of length meters winds aside at u meters along it: its waves and
// their amplitudes, the seeds of their noises; on its course as it was within straight meters of
// the faces of its falls (at faces meters along it), the waves back in full from four times that.
struct Winding {
    std::array<double, 2> wave{};
    std::array<double, 2> amplitude{};
    std::array<std::uint64_t, 2> seeds{};
    double fade = 0.0;
    double length = 0.0;
    double straight = 0.0;
    std::vector<double> faces;

    [[nodiscard]] double at(double u) const {
        double offset = 0.0;
        for (std::size_t k = 0; k < wave.size(); ++k) {
            offset += meanderAt(2.0 * u / wave[k] + shareOf(seeds[k], 40), amplitude[k], seeds[k])
                          .offset;
        }
        double envelope = smooth(0.0, fade, u) * smooth(0.0, fade, length - u);
        for (const double f : faces) {
            envelope *= smooth(straight, 4.0 * straight, std::abs(u - f));
        }
        return offset * envelope;
    }
};

Winding windingOf(std::uint64_t seed, double length, double half, double drop) {
    const double slot = half + static_cast<double>(slotBase(drop / length * kMeanRun));
    const double scale = std::max(1.0, slot / kWindRef);
    Winding w;
    w.length = length;
    w.wave[0] = (kWindWave + kWindSpread * shareOf(seed, 0)) * scale;
    for (std::size_t k = 1; k < w.wave.size(); ++k) {
        w.wave[k] = std::max(kWindLeast * scale,
                             w.wave[k - 1] * (kWindNext + kWindNextSpread * shareOf(seed, 24)));
    }
    double bend = 0.0;
    for (std::size_t k = 0; k < w.wave.size(); ++k) {
        w.seeds[k] = noise::deriveSeed(seed, k);
        w.amplitude[k] =
            slot * kWindShare[k] * (1.0 + kWindDraw * (shareOf(w.seeds[k], 0) - 0.5));
        bend += kNoiseBend * w.amplitude[k] * 4.0 / (w.wave[k] * w.wave[k]);
    }
    const double radius = std::max(kWindRadius * slot, kChannelRadius * half);
    for (double& a : w.amplitude) {
        a *= std::min(1.0, 1.0 / (radius * bend));
    }
    w.fade = std::min(kWindFade * w.wave[0], length / 4.0);
    w.straight = kFaceStraight * slot;
    return w;
}

} // namespace

void windFalls(std::vector<CoursePoint>& points, double from, double to, double half) {
    // The points of the run, every kWindStep meters at most along it (each between two points
    // of the course, as they are).
    std::size_t first = 0;
    while (first + 1 < points.size() && points[first].s < from) {
        ++first;
    }
    std::size_t last = first;
    while (last < points.size() && points[last].s <= to) {
        ++last;
    }
    if (last < first + 3) {
        return;
    }
    const double length = points[last - 1].s - points[first].s;
    const std::uint64_t seed = noise::deriveSeed(
        noise::deriveSeed(kWindSeed, static_cast<std::uint64_t>(std::llround(points[first].x))),
        static_cast<std::uint64_t>(std::llround(points[first].z)));
    Winding w = windingOf(seed, length, half,
                          static_cast<double>(points[first].level - points[last - 1].level));
    for (std::size_t i = first; i + 1 < last; ++i) {
        const CoursePoint& a = points[i];
        const CoursePoint& b = points[i + 1];
        if (a.fall && b.fall && a.level - b.level > kFaceSlope * (b.s - a.s)) {
            w.faces.push_back(0.5 * (a.s + b.s) - points[first].s);
        }
    }
    // Each segment in as many as the winding needs: its middle within kWindChord of the chord.
    std::vector<CoursePoint> run;
    for (std::size_t i = first; i < last; ++i) {
        if (i > first) {
            const CoursePoint& a = points[i - 1];
            const CoursePoint& b = points[i];
            const double from0 = a.s - points[first].s;
            const double to0 = b.s - points[first].s;
            const double chord =
                std::abs(w.at(0.5 * (from0 + to0)) - 0.5 * (w.at(from0) + w.at(to0)));
            const auto pieces =
                std::min(static_cast<int>(std::ceil((b.s - a.s) / kWindStep)),
                         1 + static_cast<int>(std::floor(std::sqrt(chord / kWindChord))));
            for (int k = 1; k < pieces; ++k) {
                run.push_back(between(a, b, a.s + (b.s - a.s) * k / pieces));
            }
        }
        run.push_back(points[i]);
    }
    std::vector<double> u(run.size(), 0.0);
    for (std::size_t i = 1; i < run.size(); ++i) {
        u[i] = run[i].s - run[0].s;
    }
    std::vector<CoursePoint> wound = run;
    for (std::size_t i = 1; i + 1 < run.size(); ++i) {
        // Aside, to the right of the course: across the samples before and after the point.
        const double dx = run[i + 1].x - run[i - 1].x;
        const double dz = run[i + 1].z - run[i - 1].z;
        const double across = dmath::hypot(dx, dz);
        if (across > 0.0) {
            const double offset = w.at(u[i]);
            wound[i].x += offset * dz / across;
            wound[i].z -= offset * dx / across;
        }
    }
    points.erase(points.begin() + static_cast<std::ptrdiff_t>(first),
                 points.begin() + static_cast<std::ptrdiff_t>(last));
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(first), wound.begin(),
                  wound.end());
}

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
