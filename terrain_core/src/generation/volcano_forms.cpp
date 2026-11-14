#include "volcano_forms.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terrain::volcano_forms {

namespace {

// The value of a lattice point in [-1, 1].
double value(std::uint64_t seed, std::uint64_t k) {
    return static_cast<double>(noise::deriveSeed(seed, k) >> 11) * 0x1.0p-52 - 1.0;
}

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// The ramp clamp(x, 0, 1) with its two corners rounded over [-e, e] and [1 - e, 1 + e] (C1).
double softRamp(double x, double e) {
    if (x <= -e) {
        return 0.0;
    }
    if (x < e) {
        return (x + e) * (x + e) / (4.0 * e);
    }
    if (x <= 1.0 - e) {
        return x;
    }
    if (x < 1.0 + e) {
        const double y = 1.0 + e - x;
        return 1.0 - y * y / (4.0 * e);
    }
    return 1.0;
}

} // namespace

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double smax(double a, double b, double k) {
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::max(a, b) + h * h * k * 0.25;
}

double smin(double a, double b, double k) {
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::min(a, b) - h * h * k * 0.25;
}

double angleBetween(double a, double b) {
    double d = std::fmod(a - b + kPi, kTwoPi);
    if (d < 0.0) {
        d += kTwoPi;
    }
    return d - kPi;
}

double stratoProfile(double rho, double sharpness) {
    if (!(rho < 1.0)) {
        return 0.0;
    }
    const double floor = dmath::exp(-sharpness);
    return (dmath::exp(-sharpness * std::max(rho, 0.0)) - floor) / (1.0 - floor);
}

double shieldProfile(double rho) {
    if (!(rho < 1.0)) {
        return 0.0;
    }
    const double u = 1.0 - rho * rho;
    return u * std::sqrt(u);
}

double ringNoise(std::uint64_t seed, double turns, int cells) {
    const int n = std::max(cells, 1);
    const double x = (turns - std::floor(turns)) * n;
    const dmath::FloorIndex i = dmath::floorIndex(x);
    const auto k0 = static_cast<std::uint64_t>(((i.index % n) + n) % n);
    const auto k1 = static_cast<std::uint64_t>(((i.index + 1) % n + n) % n);
    const double a = value(seed, k0);
    const double b = value(seed, k1);
    return a + (b - a) * fade(x - i.value);
}

double lineNoise(std::uint64_t seed, double t) {
    const dmath::FloorIndex i = dmath::floorIndex(t);
    const double a = value(seed, static_cast<std::uint64_t>(i.index));
    const double b = value(seed, static_cast<std::uint64_t>(i.index + 1));
    return a + (b - a) * fade(t - i.value);
}

double planeNoise(std::uint64_t seed, double u, double v) {
    const dmath::FloorIndex i = dmath::floorIndex(u);
    const dmath::FloorIndex j = dmath::floorIndex(v);
    const auto row = [seed](std::int64_t jj) {
        return noise::deriveSeed(seed, static_cast<std::uint64_t>(jj));
    };
    const std::uint64_t r0 = row(j.index);
    const std::uint64_t r1 = row(j.index + 1);
    const auto ia = static_cast<std::uint64_t>(i.index);
    const auto ib = static_cast<std::uint64_t>(i.index + 1);
    const double fu = fade(u - i.value);
    const double fv = fade(v - j.value);
    const double top = value(r0, ia) + (value(r0, ib) - value(r0, ia)) * fu;
    const double bottom = value(r1, ia) + (value(r1, ib) - value(r1, ia)) * fu;
    return top + (bottom - top) * fv;
}

double cinderCone(double r, double h, double radius, double crater, double depth) {
    const double top = 0.4 * radius;
    const double cone = h * softRamp((radius - r) / (radius - top), 0.15);
    if (!(r < crater) || !(depth > 0.0)) {
        return cone;
    }
    return cone - depth * (1.0 - smoothstep(0.35 * crater, crater, r));
}

double maar(double r, double depth, double radius, double ring) {
    const double q = r / radius;
    if (!(q < 2.2)) {
        return 0.0;
    }
    const double bowl = 1.0 - craterWall(q, 0.6);
    // The ring of tuff: its crest just out of the rim, its outer slope gentle.
    const double t = (q - 1.15) / (q < 1.15 ? 0.25 : 0.9);
    const double crest = std::max(0.0, 1.0 - t * t);
    return -depth * bowl + ring * crest * crest * smoothstep(0.85, 1.0, q);
}

double craterWall(double q, double wall) {
    const double w = std::clamp(wall, 0.02, 1.0);
    return smoothstep(1.0 - w, 1.0, q);
}

} // namespace terrain::volcano_forms
