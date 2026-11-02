// The winding of the slots of the falls (river_winding.hpp).
#include "river_winding.hpp"

#include "river_profile.hpp"
#include "river_slot.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace terrain::water {

namespace {

// The slot of a corridor winds aside by kWindLeast to kWindLeast + kWindSpread times the width of
// its channel at most (its farthest, drawn), over two gradient noises: waves of kWaveLeast to
// kWaveLeast + kWaveSpread widths, then kWindNext to kWindNext + kWindNextSpread of the first
// (lengths of no common measure: no bend comes back at a regular interval), each kWindShare of
// the winding. Its waves are made longer until the slot never runs at more than kRateMost meters
// aside per meter along (40 degrees) nor bends tighter than kRadiusLeast of its half width (its
// channel and the walls of its slot), measured every kFitStep meters (kFits times at most), its
// winding made smaller if they cannot be. It fades in and out over kWindFade of its longest wave
// (a quarter of the corridor at most); the corridor reaches half of that wave beyond its
// stretches, kApronLeast meters at least.
constexpr double kWindLeast = 0.5;
constexpr double kWindSpread = 1.0;
constexpr double kWaveLeast = 4.0;
constexpr double kWaveSpread = 4.0;
constexpr double kWindNext = 0.5;
constexpr double kWindNextSpread = 0.15;
constexpr std::array<double, 2> kWindShare = {1.0, 0.25};
constexpr double kRateMost = 0.839;
constexpr double kRadiusLeast = 0.75;
constexpr double kFitStep = 2.0;
constexpr int kFits = 4;
constexpr double kWindFade = 0.5;
constexpr double kApronLeast = 128.0;
// The water stays in the rock: every kHoldStep meters along the corridor, the ground (the relief,
// or the surface of a lake) every kHoldAcross meters aside, up to kBankHold meters beyond the
// channel moved aside, stands kHoldRise meters above the water at least, less the levees fill
// (kLevee, off the stretches in steps, which have none); toward a side where it would not, the
// winding is made smaller (the share of it kept there, blended over kSideBlend meters of offset
// around the axis).
constexpr double kHoldStep = 8.0;
constexpr double kHoldAcross = 4.0;
constexpr double kBankHold = 8.0;
constexpr double kHoldRise = 0.5;
constexpr double kSideBlend = 1.0;
// The winding is drawn from a table of its offset and rate every kTableStep meters, cubic between
// them (the same offset and rate at its nodes, continuous).
constexpr double kTableStep = 0.5;
// The line of the water: the corridors every kLineStep meters at most.
constexpr double kLineStep = 8.0;
constexpr std::uint64_t kWindSeed = 0x77696e6473313242ULL;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// A share in [0, 1) from 24 bits of bits, from the bit shift.
double shareOf(std::uint64_t bits, int shift) {
    return static_cast<double>((bits >> shift) & 0xffffffULL) * 0x1.0p-24;
}

double smoothOf(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double smoothRate(double t) {
    return t > 0.0 && t < 1.0 ? 6.0 * t * (1.0 - t) : 0.0;
}

// The winding of w u meters into its corridor, before the shares of its sides.
SlotShift rawAt(const SlotWinding& w, double u) {
    const double length = w.to - w.from;
    if (!(u > 0.0) || !(u < length)) {
        return {};
    }
    double n = 0.0;
    double rate = 0.0;
    for (std::size_t k = 0; k < w.wave.size(); ++k) {
        const Meander m = meanderAt(2.0 * u / w.wave[k] + w.phase[k], w.amplitude[k], w.seed[k]);
        n += m.offset;
        rate += m.rate * 2.0 / w.wave[k];
    }
    const double a = u / w.fade;
    const double b = (length - u) / w.fade;
    const double ea = smoothOf(a);
    const double eb = smoothOf(b);
    const double envelope = ea * eb;
    const double change = (smoothRate(a) * eb - ea * smoothRate(b)) / w.fade;
    return {envelope * n, change * n + envelope * rate};
}

SlotShift sidedAt(const SlotWinding& w, double u) {
    const SlotShift raw = rawAt(w, u);
    if (w.left == w.right) {
        return {raw.offset * w.left, raw.rate * w.left};
    }
    const double t = (raw.offset + kSideBlend) / (2.0 * kSideBlend);
    const double share = lerp(w.right, w.left, smoothOf(t));
    const double change = (w.left - w.right) * smoothRate(t) / (2.0 * kSideBlend);
    return {raw.offset * share, raw.rate * (share + raw.offset * change)};
}

// The table of w (SlotWinding::table): its offset and rate every kTableStep meters, both ends
// included.
void tableOf(SlotWinding& w) {
    w.step = kTableStep;
    const auto nodes = static_cast<std::size_t>(std::ceil((w.to - w.from) / w.step)) + 1;
    w.table.assign(2 * nodes, 0.0);
    for (std::size_t k = 0; k < nodes; ++k) {
        const double u = std::min(static_cast<double>(k) * w.step, w.to - w.from);
        const SlotShift shift = sidedAt(w, u);
        w.table[2 * k] = shift.offset;
        w.table[2 * k + 1] = shift.rate;
    }
}

// The winding of w u meters into its corridor, from its table.
SlotShift tableAt(const SlotWinding& w, double u) {
    const double at = u / w.step;
    const std::size_t last = w.table.size() / 2 - 1;
    const auto k = std::min(static_cast<std::size_t>(at), last - 1);
    const double t = std::clamp(at - static_cast<double>(k), 0.0, 1.0);
    const double p0 = w.table[2 * k];
    const double m0 = w.table[2 * k + 1] * w.step;
    const double p1 = w.table[2 * k + 2];
    const double m1 = w.table[2 * k + 3] * w.step;
    // Cubic Hermite: the offset and its rate at both nodes.
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double offset = (2.0 * t3 - 3.0 * t2 + 1.0) * p0 + (t3 - 2.0 * t2 + t) * m0 +
                          (3.0 * t2 - 2.0 * t3) * p1 + (t3 - t2) * m1;
    const double rate = ((6.0 * t2 - 6.0 * t) * p0 + (3.0 * t2 - 4.0 * t + 1.0) * m0 +
                         (6.0 * t - 6.0 * t2) * p1 + (3.0 * t2 - 2.0 * t) * m1) /
                        w.step;
    return {offset, rate};
}

// The point of the axis of c at s meters along it: where it lies, the left of the course there
// (unit), and its point before it.
struct AxisAt {
    double x = 0.0;
    double z = 0.0;
    double nx = 0.0;
    double nz = 0.0;
    std::size_t i = 0;
    double t = 0.0;
};

AxisAt axisAt(const Course& c, double s) {
    const std::vector<CoursePoint>& p = c.points;
    std::size_t i = static_cast<std::size_t>(
        std::upper_bound(p.begin(), p.end(), s,
                         [](double v, const CoursePoint& q) { return v < q.s; }) -
        p.begin());
    i = std::clamp<std::size_t>(i, 1, p.size() - 1) - 1;
    const CoursePoint& a = p[i];
    const CoursePoint& b = p[i + 1];
    AxisAt out;
    out.i = i;
    out.t = b.s > a.s ? std::clamp((s - a.s) / (b.s - a.s), 0.0, 1.0) : 0.0;
    out.x = lerp(a.x, b.x, out.t);
    out.z = lerp(a.z, b.z, out.t);
    const double length = dmath::hypot(b.x - a.x, b.z - a.z);
    if (length > 0.0) {
        out.nx = -(b.z - a.z) / length;
        out.nz = (b.x - a.x) / length;
    }
    return out;
}

// The waves and seeds of the corridor of the stretch from to to meters along c, its farthest
// aside (in amplitude[0], to fit) and its ends.
SlotWinding drawWinding(const Course& c, double from, double to) {
    const AxisAt start = axisAt(c, from);
    const std::uint64_t seed = noise::deriveSeed(
        noise::deriveSeed(kWindSeed, static_cast<std::uint64_t>(std::llround(start.x))),
        static_cast<std::uint64_t>(std::llround(start.z)));
    const double width = 2.0 * static_cast<double>(c.points[axisAt(c, 0.5 * (from + to)).i].half);
    SlotWinding w;
    w.wave[0] = (kWaveLeast + kWaveSpread * shareOf(seed, 24)) * width;
    w.wave[1] = w.wave[0] * (kWindNext + kWindNextSpread * shareOf(seed, 40));
    for (std::size_t k = 0; k < w.wave.size(); ++k) {
        w.seed[k] = noise::deriveSeed(seed, k);
        w.phase[k] = shareOf(w.seed[k], 40);
    }
    w.amplitude[0] = (kWindLeast + kWindSpread * shareOf(seed, 0)) * width;
    const double apron = std::max(kApronLeast, 0.5 * w.wave[0]);
    w.from = std::max(0.0, from - apron);
    w.to = std::min(c.points.back().s, to + apron);
    return w;
}

// The farthest aside, the fastest aside per meter along and the tightest bend (meters of rate
// per meter along) of the winding of w, every kFitStep meters.
struct Fit {
    double most = 0.0;
    double rate = 0.0;
    double bend = 0.0;
};

Fit fitOf(const SlotWinding& w) {
    Fit f;
    const double length = w.to - w.from;
    double before = rawAt(w, 0.0).rate;
    for (double u = kFitStep; u < length; u += kFitStep) {
        const SlotShift s = rawAt(w, u);
        f.most = std::max(f.most, std::abs(s.offset));
        f.rate = std::max(f.rate, std::abs(s.rate));
        f.bend = std::max(f.bend, std::abs(s.rate - before) / kFitStep);
        before = s.rate;
    }
    return f;
}

// Sets the amplitudes of w: its farthest aside as drawn, its waves made longer where its slot
// would run at too steep an angle to the axis or bend too tightly (half: of its channel and the
// walls of its slot), smaller if they cannot be.
void fitWinding(SlotWinding& w, double half) {
    const double length = w.to - w.from;
    const double most = w.amplitude[0];
    double scale = 0.0;
    for (int n = 0; n < kFits && length > 4.0 * kFitStep; ++n) {
        w.fade = std::min(kWindFade * w.wave[0], 0.25 * length);
        w.amplitude = kWindShare;
        const Fit f = fitOf(w);
        if (!(f.most > 0.0)) {
            break;
        }
        scale = most / f.most;
        const double steep = scale * f.rate / kRateMost;
        const double tight = scale * f.bend * kRadiusLeast * half;
        const double longer = std::max(steep, std::sqrt(tight));
        if (!(longer > 1.0) || n + 1 == kFits) {
            scale /= std::max({1.0, steep, tight});
            break;
        }
        for (double& wave : w.wave) {
            wave *= 1.02 * longer;
        }
    }
    w.fade = std::min(kWindFade * w.wave[0], 0.25 * length);
    for (std::size_t k = 0; k < w.amplitude.size(); ++k) {
        w.amplitude[k] = scale * kWindShare[k];
    }
}

// The shares of the winding of w kept toward each side of the axis of c: where the slot moved
// aside would bring the ground beside its channel (kBankHold meters beyond it) below its water.
void holdWater(SlotWinding& w, const Course& c, const Heightfield& relief,
               const Surface& surface) {
    struct Need {
        double offset = 0.0;
        double level = 0.0;
        double bank = 0.0; // the bank of the channel on the axis
        std::size_t first = 0; // its samples
        std::size_t count = 0;
    };
    std::vector<Need> needs;
    std::vector<double> xs;
    std::vector<double> zs;
    for (double s = w.from + kHoldStep; s < w.to; s += kHoldStep) {
        const double offset = rawAt(w, s - w.from).offset;
        const AxisAt a = axisAt(c, s);
        const CoursePoint& p = c.points[a.i];
        const CoursePoint& q = c.points[a.i + 1];
        const double levee = p.fall && q.fall ? 0.0 : kLevee;
        Need n{offset, lerp(p.level, q.level, a.t) - levee,
               lerp(p.half, q.half, a.t) + kBankHold, xs.size(), 0};
        const double sign = offset > 0.0 ? 1.0 : -1.0;
        for (double d = n.bank; d <= n.bank + std::abs(offset) + kHoldAcross; d += kHoldAcross) {
            xs.push_back(a.x + sign * d * a.nx);
            zs.push_back(a.z + sign * d * a.nz);
            ++n.count;
        }
        needs.push_back(n);
    }
    std::vector<float> heights(xs.size());
    relief.heightsAt(xs, zs, heights);
    for (const Need& n : needs) {
        // The first ground below the water beyond the bank on the axis (none there already).
        double allowed = std::abs(n.offset);
        for (std::size_t k = 0; k < n.count; ++k) {
            const std::size_t j = n.first + k;
            const float h = surface ? surface(xs[j], zs[j], heights[j]) : heights[j];
            if (static_cast<double>(h) < n.level + kHoldRise) {
                allowed = k == 0 ? allowed
                                 : std::min(allowed, kHoldAcross * static_cast<double>(k - 1));
                break;
            }
        }
        const double share = std::abs(n.offset) > 0.0 ? allowed / std::abs(n.offset) : 1.0;
        double& side = n.offset > 0.0 ? w.left : w.right;
        side = std::min(side, share);
    }
}

// A point of segment a-b, t of the way: all of it interpolated (its flags those of a).
CoursePoint between(const CoursePoint& a, const CoursePoint& b, double t) {
    const auto mix = [t](float u, float v) { return static_cast<float>(lerp(u, v, t)); };
    CoursePoint p = a;
    p.x = lerp(a.x, b.x, t);
    p.z = lerp(a.z, b.z, t);
    p.s = lerp(a.s, b.s, t);
    p.level = mix(a.level, b.level);
    p.valley = mix(a.valley, b.valley);
    p.ground = mix(a.ground, b.ground);
    p.top = mix(a.top, b.top);
    p.half = mix(a.half, b.half);
    p.depth = mix(a.depth, b.depth);
    p.floorHalf = mix(a.floorHalf, b.floorHalf);
    p.gorge = mix(a.gorge, b.gorge);
    p.mountain = mix(a.mountain, b.mountain);
    p.meander = mix(a.meander, b.meander);
    p.windMost = std::max(a.windMost, b.windMost);
    return p;
}

// The line of the water of c (Course::line, lineAxis).
void lineOf(Course& c) {
    c.line.clear();
    c.lineAxis.clear();
    const std::vector<CoursePoint>& p = c.points;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        const CoursePoint& a = p[i];
        const CoursePoint& b = p[i + 1];
        const int pieces = a.windMost > 0.0f || b.windMost > 0.0f
                               ? std::max(1, static_cast<int>(std::ceil((b.s - a.s) / kLineStep)))
                               : 1;
        const double length = dmath::hypot(b.x - a.x, b.z - a.z);
        const double nx = length > 0.0 ? -(b.z - a.z) / length : 0.0;
        const double nz = length > 0.0 ? (b.x - a.x) / length : 0.0;
        for (int k = 0; k < pieces; ++k) {
            CoursePoint q = k == 0 ? a : between(a, b, static_cast<double>(k) / pieces);
            c.lineAxis.push_back({q.x, q.z});
            q.shift = static_cast<float>(shiftAt(c, q.s).offset);
            q.x += static_cast<double>(q.shift) * nx;
            q.z += static_cast<double>(q.shift) * nz;
            c.line.push_back(q);
        }
    }
    c.lineAxis.push_back({p.back().x, p.back().z});
    c.line.push_back(p.back());
}

} // namespace

void windCorridors(Course& c, const Heightfield& relief, const Surface& surface,
                   const std::vector<std::array<double, 2>>& runs) {
    c.windings.clear();
    for (const auto& [from, to] : runs) {
        const SlotWinding w = drawWinding(c, from, to);
        if (!c.windings.empty() && w.from <= c.windings.back().to) {
            c.windings.back().to = std::max(c.windings.back().to, w.to);
        } else {
            c.windings.push_back(w);
        }
    }
    for (SlotWinding& w : c.windings) {
        double half = 0.0;
        for (const CoursePoint& p : c.points) {
            if (p.s >= w.from && p.s <= w.to) {
                half = std::max(half, static_cast<double>(p.half + p.slot));
            }
        }
        fitWinding(w, half);
        holdWater(w, c, relief, surface);
        tableOf(w);
        w.inner = 0.0;
        for (const CoursePoint& p : c.points) {
            if (p.s >= w.from && p.s <= w.to) {
                const double reach = slotReach(p.slot, p.half);
                w.inner = std::max(w.inner, static_cast<double>(p.half) + reach);
            }
        }
    }
    boundWindings(c);
    // The points within kWindWindow meters of the segments around them from a corridor.
    std::vector<CoursePoint>& p = c.points;
    for (std::size_t i = 0; i < p.size(); ++i) {
        p[i].shift = static_cast<float>(shiftAt(c, p[i].s).offset);
        const double from = p[i > 0 ? i - 1 : i].s - kWindWindow;
        const double to = p[std::min(i + 1, p.size() - 1)].s + kWindWindow;
        for (const SlotWinding& w : c.windings) {
            if (from < w.to && to > w.from) {
                p[i].windMost = std::max(p[i].windMost, static_cast<float>(w.most));
            }
        }
    }
    lineOf(c);
}

SlotShift shiftAt(const Course& c, double s) {
    for (const SlotWinding& w : c.windings) {
        if (s > w.from && s < w.to) {
            SlotShift out = w.table.size() >= 4 ? tableAt(w, s - w.from) : sidedAt(w, s - w.from);
            out.inner = w.inner;
            return out;
        }
    }
    return {};
}

} // namespace terrain::water
