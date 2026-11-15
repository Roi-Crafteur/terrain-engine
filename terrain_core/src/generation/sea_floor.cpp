#include "sea_floor.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::sea_floor {

double smoothMax(double a, double b, double k) {
    const double d = a - b;
    return 0.5 * (a + b + std::sqrt(d * d + k * k));
}

double smoothMin(double a, double b, double k) {
    return -smoothMax(-a, -b, k);
}

double roundedMax(double a, double b, double k) {
    const double h = std::max(0.0, k - std::abs(a - b)) / k;
    return std::max(a, b) + 0.25 * k * h * h;
}

double step(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double marginAt(const Margin& m, double d) {
    const double shelf = -m.breakDepth * d / m.shelfWidth;
    const double slope = -m.breakDepth - m.slopeGradient * (d - m.shelfWidth);
    const double profile = smoothMin(shelf, slope, m.corner);
    if (!(m.riseShare > 0.0)) {
        return profile; // an active margin: the slope goes on down, no rise
    }
    // The rise from the foot of the slope below the shelf break.
    const double footDepth = std::max(m.footDepth, m.breakDepth + 200.0);
    const double foot = m.shelfWidth + (footDepth - m.breakDepth) / m.slopeGradient;
    const double rise = -footDepth - m.riseGradient * std::max(0.0, d - foot);
    return profile + (smoothMax(profile, rise, m.corner) - profile) * m.riseShare;
}

double depthOfAge(double age) {
    const double t = std::max(0.0, age);
    return std::min(2'500.0 + 350.0 * std::sqrt(t), 6'400.0 - 3'200.0 * dmath::exp(-t / 62.8));
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
