#include "terrain/generation/cave_noise.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace terrain {

namespace {

static_assert(kCaveNoiseCount <= 8, "the known bits of a node fit in a byte");

std::int64_t nodeIndex(double meters) {
    return static_cast<std::int64_t>(std::floor(meters / kCaveNodeMeters));
}

double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

} // namespace

CaveNoiseLattice::CaveNoiseLattice(const std::array<CaveNoiseLayer, kCaveNoiseCount>& layers)
    : layers_(layers) {}

float CaveNoiseLattice::nodeValue(CaveNoise noise, std::int64_t i, std::int64_t j,
                                  std::int64_t k) const {
    const CaveNoiseLayer& layer = layers_[noise];
    const double x = static_cast<double>(i) * kCaveNodeMeters;
    const double y = static_cast<double>(j) * kCaveNodeMeters * layer.verticalScale;
    const double z = static_cast<double>(k) * kCaveNodeMeters;
    const noise::FbmParams fbm{layer.octaves, layer.frequency, 2.0, 0.5};
    return noise::fbm3D(x, y, z, layer.seed, fbm);
}

CaveNoiseCache::CaveNoiseCache(const CaveNoiseLattice& lattice, const CaveBox& box)
    : lattice_(&lattice), firstI_(nodeIndex(box.minX)), firstJ_(nodeIndex(box.minY)),
      firstK_(nodeIndex(box.minZ)), countI_(nodeIndex(box.maxX) - firstI_ + 2),
      countJ_(nodeIndex(box.maxY) - firstJ_ + 2), countK_(nodeIndex(box.maxZ) - firstK_ + 2) {
    const auto nodes = static_cast<std::size_t>(countI_ * countJ_ * countK_);
    values_.assign(nodes * kCaveNoiseCount, 0.0f);
    known_.assign(nodes, 0);
}

CaveNoiseCache::Cell CaveNoiseCache::cellOf(double x, double y, double z) const {
    const double nx = x / kCaveNodeMeters;
    const double ny = y / kCaveNodeMeters;
    const double nz = z / kCaveNodeMeters;
    const double fx = std::floor(nx);
    const double fy = std::floor(ny);
    const double fz = std::floor(nz);
    return {static_cast<std::int64_t>(fx), static_cast<std::int64_t>(fy),
            static_cast<std::int64_t>(fz), nx - fx, ny - fy, nz - fz};
}

std::size_t CaveNoiseCache::node(std::int64_t i, std::int64_t j, std::int64_t k,
                                 unsigned noises) {
    const std::int64_t li = i - firstI_;
    const std::int64_t lj = j - firstJ_;
    const std::int64_t lk = k - firstK_;
    assert(li >= 0 && li < countI_ && lj >= 0 && lj < countJ_ && lk >= 0 && lk < countK_);
    const auto index = static_cast<std::size_t>((lk * countJ_ + lj) * countI_ + li);
    const unsigned missing = noises & ~static_cast<unsigned>(known_[index]);
    if (missing != 0) {
        for (std::size_t n = 0; n < kCaveNoiseCount; ++n) {
            const auto noise = static_cast<CaveNoise>(n);
            if ((missing & caveNoiseBit(noise)) != 0) {
                values_[index * kCaveNoiseCount + n] = lattice_->nodeValue(noise, i, j, k);
            }
        }
        known_[index] = static_cast<std::uint8_t>(known_[index] | missing);
    }
    return index;
}

std::array<std::size_t, 8> CaveNoiseCache::corners(const Cell& cell, unsigned noises) {
    std::array<std::size_t, 8> indices{};
    for (int corner = 0; corner < 8; ++corner) {
        indices[static_cast<std::size_t>(corner)] =
            node(cell.i + (corner & 1), cell.j + ((corner >> 1) & 1), cell.k + (corner >> 2),
                 noises);
    }
    return indices;
}

void CaveNoiseCache::sample(double x, double y, double z, unsigned noises,
                            CaveNoiseValues& values) {
    const Cell c = cellOf(x, y, z);
    const std::array<std::size_t, 8> nodes = corners(c, noises);
    for (std::size_t n = 0; n < kCaveNoiseCount; ++n) {
        if ((noises & caveNoiseBit(static_cast<CaveNoise>(n))) == 0) {
            continue;
        }
        std::array<double, 8> v{};
        for (std::size_t corner = 0; corner < 8; ++corner) {
            v[corner] = static_cast<double>(values_[nodes[corner] * kCaveNoiseCount + n]);
        }
        const double y0 = lerp(lerp(v[0], v[1], c.fx), lerp(v[2], v[3], c.fx), c.fy);
        const double y1 = lerp(lerp(v[4], v[5], c.fx), lerp(v[6], v[7], c.fx), c.fy);
        values[n] = static_cast<float>(lerp(y0, y1, c.fz));
    }
}

void CaveNoiseCache::cellBounds(double x, double y, double z, unsigned noises,
                                CaveNoiseValues& low, CaveNoiseValues& high) {
    const std::array<std::size_t, 8> nodes = corners(cellOf(x, y, z), noises);
    for (std::size_t n = 0; n < kCaveNoiseCount; ++n) {
        if ((noises & caveNoiseBit(static_cast<CaveNoise>(n))) == 0) {
            continue;
        }
        float lowest = values_[nodes[0] * kCaveNoiseCount + n];
        float highest = lowest;
        for (std::size_t corner = 1; corner < 8; ++corner) {
            const float value = values_[nodes[corner] * kCaveNoiseCount + n];
            lowest = std::min(lowest, value);
            highest = std::max(highest, value);
        }
        low[n] = lowest;
        high[n] = highest;
    }
}

std::size_t CaveNoiseCache::cellIndex(double x, double y, double z) const {
    const Cell c = cellOf(x, y, z);
    const std::int64_t li = c.i - firstI_;
    const std::int64_t lj = c.j - firstJ_;
    const std::int64_t lk = c.k - firstK_;
    assert(li >= 0 && li + 1 < countI_ && lj >= 0 && lj + 1 < countJ_ && lk >= 0 &&
           lk + 1 < countK_);
    return static_cast<std::size_t>((lk * countJ_ + lj) * countI_ + li);
}

} // namespace terrain
