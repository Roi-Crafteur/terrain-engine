#pragma once

// What the tests of the steps of the falls share (river_steps_test, river_lips_test): the drops of
// the water along a reach, from their lip to their foot.
#include "terrain/generation/water/hydrology.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace steps {

using terrain::RiverPoint;
using terrain::RiverReach;

constexpr double kRunGap = 20.0; // a corridor: its points closer than this

// Meters along the points of a reach.
inline std::vector<double> alongOf(const RiverReach& reach) {
    const auto& p = reach.points;
    std::vector<double> s(p.size(), 0.0);
    for (std::size_t i = 1; i < p.size(); ++i) {
        s[i] = s[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
    }
    return s;
}

// The drops of the water along a reach: from the last point of a pool (the lip) to the first
// point below its face (the foot), its height. A face comes down more than kFace meters per meter
// (rapids less steeply).
struct Drop {
    std::size_t lip = 0;
    std::size_t foot = 0;
    double height = 0.0;
};

constexpr double kFace = 1.0;

inline bool faceAt(const std::vector<RiverPoint>& p, std::size_t i) {
    const double run = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
    const double drop = static_cast<double>(p[i].level - p[i + 1].level);
    return run < kRunGap && drop > 0.5 && drop > kFace * run;
}

inline std::vector<Drop> dropsOf(const RiverReach& reach) {
    const auto& p = reach.points;
    std::vector<Drop> out;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!faceAt(p, i)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && faceAt(p, j)) {
            ++j;
        }
        out.push_back({i, j, static_cast<double>(p[i].level - p[j].level)});
        i = j - 1;
    }
    return out;
}

} // namespace steps
