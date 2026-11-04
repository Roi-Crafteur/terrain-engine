#include "sea_floor.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::sea_floor {
namespace {

constexpr double kTerraceFade = 20'000.0;

} // namespace

double smoothMax(double a, double b, double k) {
    const double d = a - b;
    return 0.5 * (a + b + std::sqrt(d * d + k * k));
}

double smoothMin(double a, double b, double k) {
    return -smoothMax(-a, -b, k);
}

double step(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

namespace {

// The margin with or without its terrace, its rise from the foot of the slope below it.
double profileOf(const Margin& m, double d, bool terraced) {
    const double shelf = -m.breakDepth * d / m.shelfWidth;
    const double slope = -m.breakDepth - m.slopeGradient * (d - m.shelfWidth);
    double profile = smoothMin(shelf, slope, m.corner);
    // Where the lower slope starts and how deep: the shelf break, or the outer edge of a terrace.
    double start = m.shelfWidth;
    double top = m.breakDepth;
    if (terraced) {
        const double reach = m.shelfWidth + (m.terraceDepth - m.breakDepth) / m.slopeGradient;
        // The gentle lines stay level before their start: never above the shore.
        const double terrace = -m.terraceDepth - m.terraceGradient * std::max(0.0, d - reach);
        start = reach + m.terraceWidth;
        top = m.terraceDepth + m.terraceGradient * m.terraceWidth;
        const double lower = -top - m.slopeGradient * (d - start);
        profile = smoothMin(smoothMax(profile, terrace, m.corner), lower, m.corner);
    }
    if (!(m.riseShare > 0.0)) {
        return profile; // an active margin: the slope goes on down, no rise
    }
    const double footDepth = std::max(m.footDepth, top + 200.0);
    const double foot = start + (footDepth - top) / m.slopeGradient;
    const double rise = -footDepth - m.riseGradient * std::max(0.0, d - foot);
    return profile + (smoothMax(profile, rise, m.corner) - profile) * m.riseShare;
}

} // namespace

double marginAt(const Margin& m, double d) {
    const double plain = profileOf(m, d, false);
    if (!(m.terraceWidth > 0.0 && m.terraceDepth > m.breakDepth)) {
        return plain;
    }
    // The terrace faded in over its first kTerraceFade meters of width: nothing jumps where it
    // starts along the coast.
    const double t = step(0.0, kTerraceFade, m.terraceWidth);
    return plain + (profileOf(m, d, true) - plain) * t;
}

double depthOfAge(double age) {
    const double t = std::max(0.0, age);
    if (t < 20.0) {
        return 2'600.0 + 365.0 * std::sqrt(t);
    }
    return 5'651.0 - 2'473.0 * dmath::exp(-0.0278 * t);
}

double axisRelief(const Axis& a, double v, double rate) {
    const double across = std::abs(v);
    const double slow = step(a.slowBelow, 0.0, rate);
    const double fast = step(a.fastAbove, 1.0, rate);
    double relief = 0.0;
    if (slow > 0.0) {
        relief -= a.valleyDepth * slow * (1.0 - step(a.valleyFloor, a.valleyWidth, across));
    }
    if (fast > 0.0) {
        const double u = across / (2.0 * a.highWidth);
        const double bump = u < 1.0 ? (1.0 - u * u) * (1.0 - u * u) : 0.0;
        relief += a.highHeight * fast * bump;
    }
    return relief;
}

} // namespace terrain::sea_floor
