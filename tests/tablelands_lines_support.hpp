#pragma once

// Lines of the plane for the tests of the plateaus (agent 46): the zero lines of a field followed
// step by step, a zero near a site, and how straight a line is (its longest stretch near its chord,
// how far its stretches stray from theirs).

#include <algorithm>
#include <cmath>
#include <vector>

namespace tablelands_lines {

struct Point {
    double x = 0.0;
    double z = 0.0;
};

// The gradient of f at p by differences of h meters.
template <typename F>
Point gradientOf(const F& f, const Point& p, double h) {
    const double v = f(p.x, p.z);
    return {(f(p.x + h, p.z) - v) / h, (f(p.x, p.z + h) - v) / h};
}

// The zero line of f from start, step meters at a time, length meters in each direction (no
// farther where it is lost: its value tolerance or more off zero after four projections, and no
// zero across it within refind meters; or stalled).
template <typename F>
std::vector<Point> zeroLine(const F& f, const Point& start, double step, double length, double h,
                            double tolerance, double refind = 50.0) {
    std::vector<Point> out;
    for (const double way : {-1.0, 1.0}) {
        std::vector<Point> half;
        Point p = start;
        Point g = gradientOf(f, p, h);
        Point last{0.0, 0.0};
        for (double run = 0.0; run < length; run += step) {
            const double norm = std::hypot(g.x, g.z);
            if (!(norm > 0.0)) {
                break;
            }
            Point t{-g.z / norm * way, g.x / norm * way};
            if (run > 0.0 && t.x * last.x + t.z * last.z < 0.0) {
                t = {-t.x, -t.z};
            }
            last = t;
            Point q{p.x + step * t.x, p.z + step * t.z};
            for (int k = 0; k < 4; ++k) {
                g = gradientOf(f, q, h);
                const double n2 = g.x * g.x + g.z * g.z;
                const double v = f(q.x, q.z);
                if (!(n2 > 0.0)) {
                    break;
                }
                q = {q.x - v * g.x / n2, q.z - v * g.z / n2};
            }
            // Off its zero line: found again across it within refind meters, by bisection.
            if (!(std::abs(f(q.x, q.z)) < tolerance)) {
                const double norm2 = std::max(std::hypot(g.x, g.z), 1e-12);
                const Point n{g.x / norm2, g.z / norm2};
                Point lo{q.x - refind * n.x, q.z - refind * n.z};
                Point hi{q.x + refind * n.x, q.z + refind * n.z};
                if ((f(lo.x, lo.z) > 0.0) == (f(hi.x, hi.z) > 0.0)) {
                    break;
                }
                const bool loPositive = f(lo.x, lo.z) > 0.0;
                for (int k = 0; k < 20; ++k) {
                    const Point mid{0.5 * (lo.x + hi.x), 0.5 * (lo.z + hi.z)};
                    ((f(mid.x, mid.z) > 0.0) == loPositive ? lo : hi) = mid;
                }
                q = lo;
                g = gradientOf(f, q, h);
            }
            // Stalled: pulled back where it turns sharply.
            if (!(std::hypot(q.x - p.x, q.z - p.z) > 0.5 * step)) {
                break;
            }
            half.push_back(q);
            p = q;
        }
        if (way < 0.0) {
            out.assign(half.rbegin(), half.rend());
            out.push_back(start);
        } else {
            out.insert(out.end(), half.begin(), half.end());
        }
    }
    return out;
}

// A point on the zero line of f near site: along x, then z, the first change of sign within reach
// meters, refined by bisection.
template <typename F>
bool zeroNear(const F& f, const Point& site, double reach, Point& out) {
    for (const Point d : {Point{1.0, 0.0}, Point{0.0, 1.0}, Point{-1.0, 0.0}, Point{0.0, -1.0}}) {
        double a = 0.0;
        const double va = f(site.x, site.z);
        for (double b = 250.0; b <= reach; b += 250.0) {
            const double vb = f(site.x + d.x * b, site.z + d.z * b);
            if ((va > 0.0) == (vb > 0.0)) {
                a = b;
                continue;
            }
            double lo = a;
            double hi = b;
            for (int k = 0; k < 30; ++k) {
                const double mid = 0.5 * (lo + hi);
                const double vm = f(site.x + d.x * mid, site.z + d.z * mid);
                ((vm > 0.0) == (va > 0.0) ? lo : hi) = mid;
            }
            out = {site.x + d.x * lo, site.z + d.z * lo};
            return true;
        }
    }
    return false;
}

// The longest stretch of a line of points spaced spacing meters apart within tolerance meters of
// its chord, meters; from, its first point.
inline double longestStraight(const std::vector<Point>& line, double spacing, double tolerance,
                              Point& from) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        std::size_t j = i + 2;
        for (; j < line.size(); ++j) {
            const double cx = line[j].x - line[i].x;
            const double cz = line[j].z - line[i].z;
            const double chord = std::hypot(cx, cz);
            bool within = chord > 0.0;
            for (std::size_t k = i + 1; k < j && within; ++k) {
                const double off =
                    std::abs((line[k].x - line[i].x) * cz - (line[k].z - line[i].z) * cx) / chord;
                within = off <= tolerance;
            }
            if (!within) {
                break;
            }
        }
        if (j - 1 - i > best) {
            best = j - 1 - i;
            from = line[i];
        }
    }
    return spacing * static_cast<double>(best);
}

// Over the stretches of window meters of a line of points spaced spacing meters apart, the most
// it strays from their chord, meters: the least of them (0 for a line shorter than window), and
// their median (median).
inline double leastBend(const std::vector<Point>& line, double spacing, double window, double& median) {
    const auto span = static_cast<std::size_t>(window / spacing);
    double least = 0.0;
    std::vector<double> all;
    for (std::size_t i = 0; i + span < line.size(); i += 4) {
        const Point& a = line[i];
        const Point& b = line[i + span];
        const double cx = b.x - a.x;
        const double cz = b.z - a.z;
        const double chord = std::max(std::hypot(cx, cz), 1.0);
        double most = 0.0;
        for (std::size_t k = i + 1; k < i + span; ++k) {
            most = std::max(most, std::abs((line[k].x - a.x) * cz - (line[k].z - a.z) * cx) / chord);
        }
        least = i == 0 ? most : std::min(least, most);
        all.push_back(most);
    }
    std::sort(all.begin(), all.end());
    median = all.empty() ? 0.0 : all[all.size() / 2];
    return least;
}

// The share of the length of a line of points spaced spacing meters apart in straight stretches
// longer than least meters (within tolerance meters of their chord, their two halves turning by
// turn radians at most: a flat side, not the arc of a wide bend), taken greedily from its start.
inline double straightShare(const std::vector<Point>& line, double spacing, double tolerance,
                            double least, double turn) {
    std::size_t straight = 0;
    std::size_t i = 0;
    while (i + 1 < line.size()) {
        std::size_t j = i + 2;
        for (; j < line.size(); ++j) {
            const double cx = line[j].x - line[i].x;
            const double cz = line[j].z - line[i].z;
            const double chord = std::hypot(cx, cz);
            bool within = chord > 0.0;
            for (std::size_t k = i + 1; k < j && within; ++k) {
                within = std::abs((line[k].x - line[i].x) * cz - (line[k].z - line[i].z) * cx) /
                             chord <=
                         tolerance;
            }
            if (!within) {
                break;
            }
        }
        const std::size_t run = j - 1 - i;
        if (spacing * static_cast<double>(run) > least) {
            const Point& a = line[i];
            const Point& m = line[i + run / 2];
            const Point& b = line[j - 1];
            const double first = std::atan2(m.z - a.z, m.x - a.x);
            const double second = std::atan2(b.z - m.z, b.x - m.x);
            double bend = std::abs(second - first);
            bend = bend > 3.141592653589793 ? 2.0 * 3.141592653589793 - bend : bend;
            straight += bend <= turn ? run : 0;
        }
        i = std::max(j - 1, i + 1);
    }
    return line.size() > 1 ? static_cast<double>(straight) / static_cast<double>(line.size() - 1)
                           : 0.0;
}

} // namespace tablelands_lines
