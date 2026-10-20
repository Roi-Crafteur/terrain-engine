#include "terrain/generation/cave_region.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

CaveRegion::CaveRegion(std::uint64_t seed, const HeightmapRegion& region,
                       const CaveRegionParams& params)
    : eroded_(seed, region, params.eroded), surface_(surfaceFromEroded(eroded_)),
      field_(seed, params.caves), ecology_(seed, params.ecology),
      environment_(eroded_.field(), ecology_.climate(), params.ecology.biomes),
      speleothems_(seed, params.caves.speleothems) {
    ground_.height = [this](double x, double z) { return groundAt(x, z); };
    ground_.riverDistance = [this](double x, double z) { return riverDistanceAt(x, z); };
    ground_.environment = [this](double x, double z) { return environment_.at(x, z); };
}

float CaveRegion::riverDistanceAt(double x, double z) const {
    const int size = surface_.size();
    const double metersPerPixel = surface_.region.metersPerPixel;
    const double u = std::clamp((x - surface_.sampleX(0)) / metersPerPixel, 0.0, size - 1.0);
    const double v = std::clamp((z - surface_.sampleZ(0)) / metersPerPixel, 0.0, size - 1.0);
    const int column = std::min(static_cast<int>(u), size - 2);
    const int row = std::min(static_cast<int>(v), size - 2);
    const double fu = u - column;
    const double fv = v - row;
    const auto at = [this](int c, int r) {
        return static_cast<double>(surface_.at(c, r).riverDistance);
    };
    const double top = at(column, row) + fu * (at(column + 1, row) - at(column, row));
    const double bottom =
        at(column, row + 1) + fu * (at(column + 1, row + 1) - at(column, row + 1));
    return static_cast<float>(top + fv * (bottom - top));
}

float CaveRegion::densityAt(double x, double y, double z, const CaveDetail& detail) const {
    return caveDensityAt(field_, ground_, detail, x, y, z);
}

CaveSampler CaveRegion::sampler(const CaveBox& box, const CaveDetail& detail) const {
    return CaveSampler(field_, ground_, detail, box);
}

} // namespace terrain
