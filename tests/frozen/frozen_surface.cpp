// FROZEN REFERENCE, do not edit: surfaceFromHeightfield (with setWater, Neighbors and setSlopes)
// of terrain_core/src/generation/terrain_surface.cpp and sampleHeightGrid of height_grid.cpp as
// of commit 052078b, before the noise performance work, copied verbatim into namespace frozen:
// they read the relief point by point (Heightfield::sampleAt and heightAt).
#include "frozen_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace frozen {

using terrain::HeightGrid;
using terrain::Heightfield;
using terrain::HeightmapRegion;
using terrain::HeightSample;
using terrain::kSeaLevel;
using terrain::SurfaceSite;
using terrain::TerrainSurface;
using terrain::WaterKind;

namespace {

// Water of a column from its ground: a river sample holds its water at the ground height,
// the ocean covers the ground below sea level.
void setWater(SurfaceSite& site, bool river) {
    if (river) {
        site.water = WaterKind::River;
        site.waterLevel = site.ground;
    } else if (site.ground < kSeaLevel) {
        site.water = WaterKind::Ocean;
        site.waterLevel = kSeaLevel;
    } else {
        site.water = WaterKind::None;
        site.waterLevel = site.ground;
    }
}

// Heights of the 4 neighbors of a sample: east (+X), west, south (+Z) and north.
struct Neighbors {
    std::array<float, 4> heights;
    double runX; // distance between the east and the west neighbor
    double runZ; // distance between the south and the north neighbor
};

// Gradient by central differences and steepness from the neighbors. On a grid border the
// missing neighbor is the sample itself, with the run shortened accordingly.
void setSlopes(SurfaceSite& site, const Neighbors& n, double spacing) {
    const auto& h = n.heights;
    site.gradX =
        n.runX > 0.0 ? static_cast<float>((static_cast<double>(h[0]) - h[1]) / n.runX) : 0.0f;
    site.gradZ =
        n.runZ > 0.0 ? static_cast<float>((static_cast<double>(h[2]) - h[3]) / n.runZ) : 0.0f;
    float difference = 0.0f;
    for (const float height : h) {
        difference = std::max(difference, std::abs(height - site.ground));
    }
    site.steepness = static_cast<float>(difference / spacing);
}

} // namespace

TerrainSurface surfaceFromHeightfield(const Heightfield& field, const HeightmapRegion& region) {
    TerrainSurface surface{region, {}};
    surface.sites.reserve(static_cast<std::size_t>(region.size) *
                          static_cast<std::size_t>(region.size));
    const double step = region.metersPerPixel;
    for (int row = 0; row < region.size; ++row) {
        const double z = surface.sampleZ(row);
        for (int column = 0; column < region.size; ++column) {
            const double x = surface.sampleX(column);
            const HeightSample sample = field.sampleAt(x, z);
            SurfaceSite site;
            site.ground = sample.height;
            site.continentalness = sample.continentalness;
            const Neighbors neighbors{{field.heightAt(x + step, z), field.heightAt(x - step, z),
                                       field.heightAt(x, z + step), field.heightAt(x, z - step)},
                                      2.0 * step,
                                      2.0 * step};
            setSlopes(site, neighbors, step);
            setWater(site, false);
            surface.sites.push_back(site);
        }
    }
    return surface;
}

HeightGrid sampleHeightGrid(const Heightfield& field, const HeightmapRegion& region) {
    HeightGrid grid{region.size, region.metersPerPixel, {}};
    grid.heights.reserve(static_cast<std::size_t>(region.size) *
                         static_cast<std::size_t>(region.size));
    const double half = region.size / 2.0;
    for (int row = 0; row < region.size; ++row) {
        const double z = region.centerZ + (row + 0.5 - half) * region.metersPerPixel;
        for (int column = 0; column < region.size; ++column) {
            const double x = region.centerX + (column + 0.5 - half) * region.metersPerPixel;
            grid.heights.push_back(field.heightAt(x, z));
        }
    }
    return grid;
}

} // namespace frozen
