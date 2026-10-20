#include "terrain/generation/eroded_region.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Streams of the erosion droplets and of the flow routing, distinct from the Heightfield
// layers.
constexpr std::uint64_t kErosionStream = 0x45524F44; // "EROD"
constexpr std::uint64_t kRiverStream = 0x52495652;   // "RIVR"

} // namespace

ErodedRegion::ErodedRegion(std::uint64_t seed, const HeightmapRegion& region,
                           const ErodedRegionParams& params, const WorldSize& world)
    : field_(seed, params.heightfield, world), region_(validatedRegion(region, world)),
      raw_(sampleHeightGrid(field_, region)), grid_(raw_) {
    erodeHeightGrid(grid_, noise::deriveSeed(seed, kErosionStream), params.erosion);
    rivers_ = generateRivers(grid_, noise::deriveSeed(seed, kRiverStream), params.rivers);
}

double ErodedRegion::sampleX(int column) const {
    return region_.centerX + (column + 0.5 - region_.size / 2.0) * region_.metersPerPixel;
}

double ErodedRegion::sampleZ(int row) const {
    return region_.centerZ + (row + 0.5 - region_.size / 2.0) * region_.metersPerPixel;
}

double ErodedRegion::imageX(double x) const {
    return nearestImage(x, region_.centerX, field_.world().x);
}

double ErodedRegion::imageZ(double z) const {
    return nearestImage(z, region_.centerZ, field_.world().z);
}

float ErodedRegion::heightAt(double worldX, double worldZ) const {
    const double x = imageX(worldX);
    const double z = imageZ(worldZ);
    const float base = field_.heightAt(x, z);
    const double gx = (x - region_.centerX) / region_.metersPerPixel + region_.size / 2.0 - 0.5;
    const double gz = (z - region_.centerZ) / region_.metersPerPixel + region_.size / 2.0 - 0.5;
    const double last = region_.size - 1;
    if (!(gx >= 0.0 && gz >= 0.0 && gx <= last && gz <= last) || region_.size < 2) {
        return base;
    }
    const int i = std::min(static_cast<int>(gx), region_.size - 2);
    const int j = std::min(static_cast<int>(gz), region_.size - 2);
    const double u = gx - i;
    const double v = gz - j;
    const auto change = [&](int column, int row) {
        return static_cast<double>(grid_.at(column, row)) - raw_.at(column, row);
    };
    const double delta = change(i, j) * (1 - u) * (1 - v) + change(i + 1, j) * u * (1 - v) +
                         change(i, j + 1) * (1 - u) * v + change(i + 1, j + 1) * u * v;
    return static_cast<float>(base + delta);
}

} // namespace terrain
