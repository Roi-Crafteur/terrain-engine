#pragma once

// The buttes of the plateaus of the default world switched on (agent 46, tableland_relief.hpp), for
// the tests of their shapes and of their cliffs: found on a lattice around the dry edges, and the
// profile of their cliffs along rays.

#include "tablelands_fields_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tablelands_buttes {

using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;
using terrain::tableland_relief::Fields;

// A butte holds at most kButteCells cells of kCell meters of top. Along a ray its cliff runs from
// its rim (the first fall steeper than kSteep60) as far as the ground falls more steeply than that
// over the next kCliffRun meters (its ledges within), its own fall to the lowest point of the ray
// within kTalusReach meters (no farther than where it climbs kRiseBack meters again: another butte).
inline constexpr double kSteep60 = 1.7320508;
inline constexpr std::size_t kButteCells = 12;
inline constexpr double kCell = 250.0;
inline constexpr double kCliffRun = 30.0;
inline constexpr double kTalusReach = 400.0;
inline constexpr double kRiseBack = 30.0;

// The steepest slope over 2 m along the ray from (x, z) in direction (dx, dz), up to length.
inline double steepestAlong(double x, double z, double dx, double dz, double length) {
    constexpr double kStepMeters = 2.0;
    double steepest = 0.0;
    double previous = heightAt(x, z);
    for (double s = kStepMeters; s <= length; s += kStepMeters) {
        const double h = heightAt(x + dx * s, z + dz * s);
        steepest = std::max(steepest, std::abs(h - previous) / kStepMeters);
        previous = h;
    }
    return steepest;
}

struct Butte {
    double x = 0.0;
    double z = 0.0;
    double height = 0.0;
    double wall = 0.0;  // the median of the steepest slopes of its sides
    double cliff = 0.0; // the median share of its height its cliff takes
};

// The cliff along a ray: the heights every step meters from its start, the indices of its rim and
// of the end of its cliff (its foot), its lowest point within kTalusReach of its foot.
struct CliffRay {
    std::vector<double> h;
    double step = 2.0;
    std::size_t rim = 0;
    std::size_t end = 0;
    double lowest = 0.0;
    bool found = false;
};

inline CliffRay cliffAlong(double x, double z, double dx, double dz, double step) {
    CliffRay c;
    c.step = step;
    for (double s = 0.0; s <= 1'500.0; s += step) {
        c.h.push_back(heightAt(x + dx * s, z + dz * s));
    }
    const std::vector<double>& h = c.h;
    const auto cliffSteps = static_cast<std::size_t>(kCliffRun / step);
    std::size_t rim = 0;
    while (rim + 1 < h.size() && !((h[rim] - h[rim + 1]) / step > kSteep60)) {
        ++rim;
    }
    std::size_t end = rim;
    while (end + cliffSteps < h.size() && (h[end] - h[end + cliffSteps]) / kCliffRun > kSteep60) {
        ++end;
    }
    // Its foot: where the steep fall it ends on gives way.
    while (end + 1 < h.size() && (h[end] - h[end + 1]) / step > kSteep60) {
        ++end;
    }
    double lowest = h[end];
    const auto reach = static_cast<std::size_t>(kTalusReach / step);
    for (std::size_t k = end; k < h.size() && k < end + reach && h[k] < lowest + kRiseBack; ++k) {
        lowest = std::min(lowest, h[k]);
    }
    c.rim = rim;
    c.end = end;
    c.lowest = lowest;
    c.found = end + 1 < h.size() && h[rim] > lowest;
    return c;
}

// The share of its own fall a cliff takes along the ray (its own fall: whatever the benches of its
// talus, not the height of the butte over a ground a slope may lower farther).
inline double cliffShareAlong(double x, double z, double dx, double dz) {
    const CliffRay c = cliffAlong(x, z, dx, dz, 2.0);
    return c.found ? (c.h[c.rim] - c.h[c.end]) / (c.h[c.rim] - c.lowest) : 0.0;
}

// The outliers of the box of side cells cells around (x, z): their tops (profile 1, beyond the
// escarpment), as connected groups of cells; the small ones are buttes.
inline std::vector<Butte> buttesAround(double x0, double z0, int side) {
    std::vector<int> top(static_cast<std::size_t>(side * side), 0);
    std::vector<double> low;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const double x = x0 + kCell * (i - side / 2);
            const double z = z0 + kCell * (j - side / 2);
            const Fields f = fieldsAt(x, z);
            top[static_cast<std::size_t>(j * side + i)] =
                f.profile >= 0.999 && f.scarp < -300.0 && f.canyon == 0.0 ? 1 : 0;
        }
    }
    std::vector<Butte> out;
    std::vector<int> stack;
    for (int start = 0; start < side * side; ++start) {
        if (top[static_cast<std::size_t>(start)] != 1) {
            continue;
        }
        // A group of top cells, flooded (marked 2).
        std::vector<int> cells;
        stack.push_back(start);
        top[static_cast<std::size_t>(start)] = 2;
        bool edge = false;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            cells.push_back(c);
            const int i = c % side;
            const int j = c / side;
            edge = edge || i == 0 || j == 0 || i == side - 1 || j == side - 1;
            for (const std::array<int, 2> d : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const int ni = i + d[0];
                const int nj = j + d[1];
                if (ni >= 0 && nj >= 0 && ni < side && nj < side &&
                    top[static_cast<std::size_t>(nj * side + ni)] == 1) {
                    top[static_cast<std::size_t>(nj * side + ni)] = 2;
                    stack.push_back(nj * side + ni);
                }
            }
        }
        if (edge || cells.size() > kButteCells) {
            continue;
        }
        Butte b;
        double topHeight = -1e9;
        for (const int c : cells) {
            const double x = x0 + kCell * (c % side - side / 2);
            const double z = z0 + kCell * (c / side - side / 2);
            b.x += x / static_cast<double>(cells.size());
            b.z += z / static_cast<double>(cells.size());
            topHeight = std::max(topHeight, heightAt(x, z));
        }
        // The low ground: the lowest of a ring 1.5 km around.
        double ground = 1e9;
        for (int k = 0; k < 16; ++k) {
            const double a = 2.0 * 3.141592653589793 * k / 16.0;
            ground = std::min(ground, heightAt(b.x + 1500.0 * std::cos(a), b.z + 1500.0 * std::sin(a)));
        }
        b.height = topHeight - ground;
        std::array<double, 8> walls{};
        for (std::size_t k = 0; k < walls.size(); ++k) {
            const double a = 2.0 * 3.141592653589793 * static_cast<double>(k) / 8.0;
            walls[k] = steepestAlong(b.x, b.z, std::cos(a), std::sin(a), 900.0);
        }
        std::sort(walls.begin(), walls.end());
        b.wall = 0.5 * (walls[3] + walls[4]);
        out.push_back(b);
    }
    return out;
}

// The median share of the height of a butte its cliff takes, over 8 rays.
inline double cliffShareOf(const Butte& b) {
    std::array<double, 8> shares{};
    for (std::size_t k = 0; k < shares.size(); ++k) {
        const double a = 2.0 * 3.141592653589793 * static_cast<double>(k) / 8.0;
        shares[k] = cliffShareAlong(b.x, b.z, std::cos(a), std::sin(a));
    }
    std::sort(shares.begin(), shares.end());
    return 0.5 * (shares[3] + shares[4]);
}


// Whether b stands as a butte walled by its cliff: on half of kWalledRays rays at least, a cliff
// kWalledLeast meters high at least and half as high as the tallest, its rim out of the canyons (a
// canyon through it, its walls those of the canyon, is not its cliff; nor a hill with one wall).
inline constexpr int kWalledRays = 16;
inline constexpr double kWalledLeast = 30.0;
inline bool walled(const Butte& b) {
    std::array<double, kWalledRays> falls{};
    for (int k = 0; k < kWalledRays; ++k) {
        const double a = 6.283185307179586 * (k + 0.5) / kWalledRays;
        const CliffRay c = cliffAlong(b.x, b.z, std::cos(a), std::sin(a), 2.0);
        const double rim = 2.0 * static_cast<double>(c.rim);
        const bool canyon = fieldsAt(b.x + std::cos(a) * rim, b.z + std::sin(a) * rim).canyon > 0.0;
        falls[static_cast<std::size_t>(k)] = c.found && !canyon ? c.h[c.rim] - c.h[c.end] : 0.0;
    }
    const double tallest = *std::max_element(falls.begin(), falls.end());
    const auto held = std::count_if(falls.begin(), falls.end(), [&](double f) {
        return f >= kWalledLeast && f >= 0.5 * tallest;
    });
    return held >= kWalledRays / 2;
}

// The buttes of the case of the tests of the cliffs: from the dry edges of sites in their order,
// those from low to high meters high whose walls are steeper than 60 degrees and that stand walled
// by their cliff, count of them at most (sought of the others looked at at most).
inline std::vector<Butte> caseButtes(const tablelands_fields::Sites& sites, double low, double high,
                                     std::size_t count, std::size_t sought) {
    std::vector<Butte> out;
    std::size_t seen = 0;
    for (std::size_t e = 0; e < sites.edges.size() && out.size() < count && seen < sought; ++e) {
        for (const Butte& b : buttesAround(sites.edges[e][0], sites.edges[e][1], 96)) {
            if (out.size() < count && seen < sought && b.height >= low && b.height <= high &&
                b.wall > kSteep60) {
                ++seen;
                if (walled(b)) {
                    out.push_back(b);
                }
            }
        }
    }
    return out;
}

} // namespace tablelands_buttes
