#pragma once

// The measures of a volcano on the relief (agent 45): along rays from its center, with and
// without the volcanoes, its rise over the ground it stands on, its slopes over 500 m, its
// crater, the outline of its foot. Shared by the tests of the forms and the export of the
// profiles.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace volcano_measure {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr int kRays = 48;

inline double degrees(double tangent) {
    return std::atan(tangent) * 180.0 / kPi;
}

inline double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// A ray from the center of a volcano: its heights with and without the volcanoes, step apart.
struct Ray {
    double angle = 0.0;
    double step = 0.0;
    std::vector<float> with;
    std::vector<float> without;
};

inline Ray rayOf(const terrain::Heightfield& with, const terrain::Heightfield& without,
                 const terrain::VolcanoSite& s, double angle, double length, double step) {
    Ray r;
    r.angle = angle;
    r.step = step;
    const auto n = static_cast<std::size_t>(length / step) + 1;
    std::vector<double> xs(n);
    std::vector<double> zs(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = s.x + std::cos(angle) * step * static_cast<double>(i);
        zs[i] = s.z + std::sin(angle) * step * static_cast<double>(i);
    }
    r.with.resize(n);
    r.without.resize(n);
    with.heightsAt(xs, zs, r.with);
    without.heightsAt(xs, zs, r.without);
    return r;
}

struct Measure {
    double base = 0.0;   // the mean ground without the volcanoes within its foot
    double summit = 0.0; // the highest point
    double summitOff = 0.0; // how far from the center, over the radius
    double rise = 0.0;      // summit - base
    double steepest = 0.0;  // degrees, over 500 m along the rays (out of the crater)
    double upper = 0.0;     // degrees, the median over the rays of the steepest within 0.4 R
    double foot = 0.0;      // degrees, the median slope from 0.75 R to R
    double crater = 0.0;    // the depth of its crater or caldera under its rim
    double footMin = 0.0;   // the radius where the ground falls to 5 % of the rise, over the rays
    double footMax = 0.0;
    double lowest = 0.0;    // the lowest ground within the foot, under the ground before
};

// Its base (the mean ground without it within its foot), its summit and how far off the center,
// its rise, the lowest it digs under the ground before it.
inline void summitOf(const std::vector<Ray>& rays, const terrain::VolcanoSite& s, double step,
                     Measure& m) {
    const double radius = static_cast<double>(s.radius);
    double sum = 0.0;
    int count = 0;
    m.summit = -1e9;
    for (const Ray& r : rays) {
        for (std::size_t i = 0; i < r.with.size(); ++i) {
            const double d = step * static_cast<double>(i);
            if (d <= radius) {
                sum += r.without[i];
                ++count;
                m.lowest = std::min(m.lowest, static_cast<double>(r.with[i] - r.without[i]));
            }
            if (d <= 0.5 * radius * static_cast<double>(s.elongation) && r.with[i] > m.summit) {
                m.summit = r.with[i];
                m.summitOff = d / radius;
            }
        }
    }
    m.base = s.atSea ? static_cast<double>(s.floor) : sum / count;
    m.rise = m.summit - m.base;
}

// Along rays from its summit (volcanoSummit: its crater around it).
inline Measure measure(const terrain::Heightfield& with, const terrain::Heightfield& without,
                       const terrain::VolcanoSite& site) {
    terrain::VolcanoSite s = site;
    const terrain::VolcanoOffset top = terrain::volcanoSummit(site);
    s.x += top.dx;
    s.z += top.dz;
    const double radius = static_cast<double>(s.radius);
    const double length = 1.3 * radius * static_cast<double>(s.elongation);
    const double step = std::max(25.0, radius / 200.0);
    const auto span = static_cast<std::size_t>(std::max(1.0, std::round(500.0 / step)));
    std::vector<Ray> rays;
    for (int k = 0; k < kRays; ++k) {
        rays.push_back(rayOf(with, without, s, 2.0 * kPi * k / kRays, length, step));
    }
    Measure m;
    summitOf(rays, s, step, m);
    const double crater = static_cast<double>(s.crater);
    std::vector<double> uppers;
    std::vector<double> feet;
    std::vector<double> rims;
    std::vector<double> floors;
    m.footMin = 1e9;
    for (const Ray& r : rays) {
        double upper = 0.0;
        double rim = -1e9;
        double floor = 1e9;
        std::size_t footAt = r.with.size() - 1;
        for (std::size_t i = 0; i + span < r.with.size(); ++i) {
            const double d = step * static_cast<double>(i);
            const double slope =
                std::abs(r.with[i + span] - r.with[i]) / (step * static_cast<double>(span));
            if (d > 1.3 * crater) {
                m.steepest = std::max(m.steepest, degrees(slope));
                // The upper flank: the first 0.4 of the radius, at least a kilometer out of the
                // rim (a wide amphitheater).
                if (d < std::max(0.4 * radius, 1.3 * crater + 1000.0)) {
                    upper = std::max(upper, slope);
                }
            }
            if (d < 1.4 * crater) {
                rim = std::max(rim, static_cast<double>(r.with[i]));
            }
            if (d < 0.6 * crater) {
                floor = std::min(floor, static_cast<double>(r.with[i]));
            }
        }
        for (std::size_t i = 0; i < r.with.size(); ++i) {
            if (step * static_cast<double>(i) > m.summitOff * radius &&
                r.with[i] < m.base + 0.05 * m.rise) {
                footAt = i;
                break;
            }
        }
        const auto at = [&](double rho) {
            return std::min(r.with.size() - 1,
                            static_cast<std::size_t>(rho * radius / step));
        };
        feet.push_back(std::abs(r.with[at(0.75)] - r.with[at(1.0)]) / (0.25 * radius));
        uppers.push_back(upper);
        rims.push_back(rim);
        floors.push_back(floor);
        m.footMin = std::min(m.footMin, step * static_cast<double>(footAt));
        m.footMax = std::max(m.footMax, step * static_cast<double>(footAt));
    }
    m.upper = degrees(median(uppers));
    m.foot = degrees(median(feet));
    if (crater > 0.0) {
        m.crater = median(rims) - *std::min_element(floors.begin(), floors.end());
    }
    return m;
}

} // namespace volcano_measure
