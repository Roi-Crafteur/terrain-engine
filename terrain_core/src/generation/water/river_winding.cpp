// The winding of the runs where the water falls (river_winding.hpp).
#include "river_winding.hpp"

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
// The winding is full where the valley comes down kCalmSlope meters per meter at most (calmOf).
constexpr double kCalmSlope = 0.1;
constexpr double kCalmSpan = 20.0;
// The run is sampled every kWindStep meters at most where it winds most: a segment is cut in as
// many as bring the middle of the winding within kWindChord meters of its chord.
constexpr double kWindStep = 4.0;
constexpr double kWindChord = 0.25;
constexpr std::uint64_t kWindSeed = 0x77696e6473313241ULL;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// A share in [0, 1) from 24 bits of bits, from the bit shift.
double shareOf(std::uint64_t bits, int shift) {
    return static_cast<double>((bits >> shift) & 0xffffffULL) * 0x1.0p-24;
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

// How much a run winds at each of its points (u meters along it): fully where its valley comes
// down kCalmSlope meters per meter or less (over kCalmSpan meters on each side), less on steeper
// ground (the square of kCalmSlope over its slope): there the floor of the valley beside the slot
// would step where its segments turn.
std::vector<double> calmOf(const std::vector<CoursePoint>& run, const std::vector<double>& u) {
    const auto valleyAt = [&](double at) {
        std::size_t i = 0;
        while (i + 2 < run.size() && u[i + 1] < at) {
            ++i;
        }
        const double t = u[i + 1] > u[i] ? std::clamp((at - u[i]) / (u[i + 1] - u[i]), 0.0, 1.0)
                                         : 0.0;
        return lerp(run[i].valley, run[i + 1].valley, t);
    };
    std::vector<double> out(run.size(), 1.0);
    for (std::size_t i = 0; i < run.size(); ++i) {
        const double slope =
            (valleyAt(u[i] - kCalmSpan) - valleyAt(u[i] + kCalmSpan)) / (2.0 * kCalmSpan);
        const double share = kCalmSlope / std::max(kCalmSlope, slope);
        out[i] = share * share;
    }
    return out;
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
    const std::vector<double> calm = calmOf(run, u);
    std::vector<CoursePoint> wound = run;
    for (std::size_t i = 1; i + 1 < run.size(); ++i) {
        // Aside, to the right of the course: across the samples before and after the point.
        const double dx = run[i + 1].x - run[i - 1].x;
        const double dz = run[i + 1].z - run[i - 1].z;
        const double across = dmath::hypot(dx, dz);
        if (across > 0.0) {
            const double offset = w.at(u[i]) * calm[i];
            wound[i].x += offset * dz / across;
            wound[i].z -= offset * dx / across;
        }
    }
    points.erase(points.begin() + static_cast<std::ptrdiff_t>(first),
                 points.begin() + static_cast<std::ptrdiff_t>(last));
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(first), wound.begin(),
                  wound.end());
}

} // namespace terrain::water
