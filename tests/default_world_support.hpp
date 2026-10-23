#pragma once

// The default world (the planetary geography of generator 3) for the tests that check a module
// there: its places are found by search, ring by ring around the origin, never written in.

#include "terrain/generation/heightfield.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>

namespace default_world {

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

inline const terrain::Heightfield& field() {
    static const terrain::Heightfield f(kSeed);
    return f;
}

struct Place {
    double x = 0.0;
    double z = 0.0;
};

// The first point of a lattice of step meters around the origin, ring by ring out to rings
// steps, that passes the test (row by row within a ring).
inline std::optional<Place> nearest(double step, int rings,
                                    const std::function<bool(double, double)>& wanted) {
    for (int r = 0; r <= rings; ++r) {
        for (int j = -r; j <= r; ++j) {
            for (int i = -r; i <= r; ++i) {
                if (std::max(std::abs(i), std::abs(j)) == r && wanted(i * step, j * step)) {
                    return Place{i * step, j * step};
                }
            }
        }
    }
    return std::nullopt;
}

// Mountains: the mountain layer above 1,500 m (lattice of 4 km, out to 1,000 km).
inline std::optional<Place> mountain() {
    return nearest(4096.0, 250,
                   [](double x, double z) { return field().sampleAt(x, z).mountain > 1500.0f; });
}

// A coast: the sea, and land 1 km east (lattice of 1 km, out to 200 km).
inline std::optional<Place> coast() {
    return nearest(1024.0, 200, [](double x, double z) {
        return field().heightAt(x, z) < terrain::kSeaLevel &&
               field().heightAt(x + 1024.0, z) > terrain::kSeaLevel;
    });
}

// A plain: land over 100 km from the coast, where the relief of the geography is the weakest
// (an intensity of at most 0.06) and no belt reaches (lattice of 8 km, out to 2,000 km).
inline std::optional<Place> plain() {
    return nearest(8192.0, 250, [](double x, double z) {
        const terrain::GeographySample g = field().geography()->sampleAt(x, z);
        return g.coast > 100'000.0 && g.relief <= 0.06f && g.belt == 0.0f;
    });
}

// Highlands: ground between low and high meters (lattice of 2 km, out to 1,000 km).
inline std::optional<Place> highlands(float low, float high,
                                      const std::function<bool(double, double)>& also) {
    return nearest(2048.0, 500, [&](double x, double z) {
        const float h = field().heightAt(x, z);
        return h >= low && h <= high && also(x, z);
    });
}

} // namespace default_world
