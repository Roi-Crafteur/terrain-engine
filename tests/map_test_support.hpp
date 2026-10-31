#pragma once

// A world for the tests of the dev map that needs no generation: every sample is a simple
// function of its pixel center, so the images do not change when the world does.

#include "terrain/map/map_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace map_test {

// A slope rising 0.05 m per meter east and 0.03 m per meter south, the sea below -20 m, a lake
// at 50 m over [100, 200] x [100, 200], a river along x = 300 (4 m wide), a fall mark of 3 m
// around the origin, and bands of biomes 100 m high.
inline float groundAt(double x, double z) {
    return static_cast<float>(0.05 * x + 0.03 * z);
}

inline terrain::MapSample syntheticSample(double x, double z, double step) {
    terrain::MapSample s;
    s.ground = groundAt(x, z);
    s.biome = static_cast<terrain::Biome>(
        1 + static_cast<int>(std::floor(std::abs(s.ground) / 100.0f)) % (terrain::kBiomeCount - 1));
    if (s.ground < -20.0f) {
        s.water = terrain::WaterKind::Ocean;
        s.waterLevel = 0.0f;
        s.biome = terrain::Biome::Ocean;
    } else if (x >= 100.0 && x < 200.0 && z >= 100.0 && z < 200.0) {
        s.water = terrain::WaterKind::Lake;
        s.waterLevel = 50.0f;
    }
    const double fromRiver = std::abs(x - 300.0);
    if (fromRiver < 2.0 + 0.5 * step) {
        s.discharge = 500.0f;
        s.riverCover = static_cast<float>(std::min(1.0, (2.0 + 0.5 * step - fromRiver) / step));
    }
    const double fromFall = std::sqrt(x * x + z * z);
    if (fromFall < 3.0) {
        s.fallDrop = 120.0f;
        s.fallMark = fromFall < 1.5 ? terrain::kMapFallCenter : terrain::kMapFallRing;
    }
    return s;
}

class SyntheticSampler : public terrain::MapSampler {
public:
    void sample(const terrain::MapFrame& frame, std::span<terrain::MapSample> out, int,
                terrain::MapSampleStats& stats) const override {
        if (out.size() != terrain::mapGridSize(frame)) {
            throw std::invalid_argument("synthetic sampler: wrong grid size");
        }
        for (int j = -1; j <= frame.height(); ++j) {
            for (int i = -1; i <= frame.width(); ++i) {
                out[terrain::mapGridIndex(frame, i, j)] =
                    syntheticSample(frame.pixelX(i), frame.pixelZ(j), frame.stepX());
            }
        }
        stats.samples += out.size();
    }
};

// 64-bit FNV-1a of the bytes of an image.
inline std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const std::uint8_t b : bytes) {
        h = (h ^ b) * 1099511628211ULL;
    }
    return h;
}

} // namespace map_test
