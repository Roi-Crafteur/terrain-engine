#include "terrain/generation/terrain_surface.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace terrain {

namespace {

// A wall of the cliffs is a drop of at least this share of a stratum to a neighbor sample,
// as in the cliff preview.
constexpr float kWallDropShare = 0.5f;

constexpr float kDiagonal = 1.41421356f;

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

// Chamfer distance transform (8-connected) from the river samples, in meters, capped at
// kNoRiverDistance: a forward and a backward pass, each relaxing 4 already visited neighbors.
std::vector<float> riverDistances(std::span<const std::uint8_t> river, int size,
                                  double metersPerPixel) {
    const auto count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (river.empty()) {
        return std::vector<float>(count, kNoRiverDistance);
    }
    std::vector<float> distance(count, std::numeric_limits<float>::infinity());
    for (std::size_t i = 0; i < count; ++i) {
        if (river[i] != 0) {
            distance[i] = 0.0f;
        }
    }
    const auto straight = static_cast<float>(metersPerPixel);
    const float diagonal = straight * kDiagonal;
    const auto relax = [&](int column, int row, int dc, int dr, float step) {
        const int c = column + dc;
        const int r = row + dr;
        if (c >= 0 && r >= 0 && c < size && r < size) {
            float& d = distance[static_cast<std::size_t>(row * size + column)];
            d = std::min(d, distance[static_cast<std::size_t>(r * size + c)] + step);
        }
    };
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            relax(column, row, -1, 0, straight);
            relax(column, row, -1, -1, diagonal);
            relax(column, row, 0, -1, straight);
            relax(column, row, 1, -1, diagonal);
        }
    }
    for (int row = size - 1; row >= 0; --row) {
        for (int column = size - 1; column >= 0; --column) {
            relax(column, row, 1, 0, straight);
            relax(column, row, 1, 1, diagonal);
            relax(column, row, 0, 1, straight);
            relax(column, row, -1, 1, diagonal);
        }
    }
    for (float& d : distance) {
        d = std::min(d, kNoRiverDistance);
    }
    return distance;
}

// Samples whose surface differs by at least drop from a 4-neighbor where the cliff mask of
// either is above 0: the top and the foot of every vertical wall.
std::vector<std::uint8_t> cliffWalls(const HeightGrid& surface, const std::vector<float>& mask,
                                     float drop) {
    const int size = surface.size;
    std::vector<std::uint8_t> walls(surface.heights.size(), 0);
    constexpr std::array<std::array<int, 2>, 4> kSides = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const std::size_t i = surface.index(column, row);
            for (const auto& [dc, dr] : kSides) {
                const int c = column + dc;
                const int r = row + dr;
                if (c < 0 || r < 0 || c >= size || r >= size) {
                    continue;
                }
                const std::size_t n = surface.index(c, r);
                if (std::abs(surface.heights[i] - surface.heights[n]) >= drop &&
                    (mask[i] > 0.0f || mask[n] > 0.0f)) {
                    walls[i] = 1;
                }
            }
        }
    }
    return walls;
}

bool flagged(std::span<const std::uint8_t> mask, std::size_t index) {
    return !mask.empty() && mask[index] != 0;
}

// The relief of the sites of a surface: the sample of each site, and the heights of its east,
// west, south and north neighbors one pixel away, through the batch functions of the
// Heightfield. A neighbor whose coordinates are those of an adjacent site bit for bit takes the
// height of its sample (heightAt only depends on them); the others are computed. Nothing
// outlives the call.
struct SurfaceRelief {
    std::vector<HeightSample> samples;           // by site
    std::vector<std::array<float, 4>> neighbors; // by site
};

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

// The neighbors to compute: their coordinates, and where their heights go.
struct NeighborQueries {
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<float*> targets;
};

SurfaceRelief surfaceRelief(const Heightfield& field, const TerrainSurface& surface) {
    const int size = surface.size();
    const auto count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    const double step = surface.region.metersPerPixel;
    std::vector<double> xs(static_cast<std::size_t>(size));
    std::vector<double> zs(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i) {
        xs[static_cast<std::size_t>(i)] = surface.sampleX(i);
        zs[static_cast<std::size_t>(i)] = surface.sampleZ(i);
    }
    SurfaceRelief relief{std::vector<HeightSample>(count),
                         std::vector<std::array<float, 4>>(count)};
    std::vector<double> rowZ(xs.size());
    for (int row = 0; row < size; ++row) {
        std::fill(rowZ.begin(), rowZ.end(), zs[static_cast<std::size_t>(row)]);
        field.samplesAt(xs, rowZ,
                        std::span(relief.samples).subspan(surface.index(0, row), xs.size()));
    }
    NeighborQueries queries;
    const auto neighbor = [&](int column, int row, double x, double z, float& target) {
        if (column >= 0 && row >= 0 && column < size && row < size &&
            sameBits(x, xs[static_cast<std::size_t>(column)]) &&
            sameBits(z, zs[static_cast<std::size_t>(row)])) {
            target = relief.samples[surface.index(column, row)].height;
            return;
        }
        queries.xs.push_back(x);
        queries.zs.push_back(z);
        queries.targets.push_back(&target);
    };
    for (int row = 0; row < size; ++row) {
        const double z = zs[static_cast<std::size_t>(row)];
        for (int column = 0; column < size; ++column) {
            const double x = xs[static_cast<std::size_t>(column)];
            std::array<float, 4>& heights = relief.neighbors[surface.index(column, row)];
            neighbor(column + 1, row, x + step, z, heights[0]);
            neighbor(column - 1, row, x - step, z, heights[1]);
            neighbor(column, row + 1, x, z + step, heights[2]);
            neighbor(column, row - 1, x, z - step, heights[3]);
        }
    }
    std::vector<float> computed(queries.xs.size());
    field.heightsAt(queries.xs, queries.zs, computed);
    for (std::size_t i = 0; i < computed.size(); ++i) {
        *queries.targets[i] = computed[i];
    }
    return relief;
}

} // namespace

double TerrainSurface::sampleX(int column) const {
    return region.centerX + (column + 0.5 - region.size / 2.0) * region.metersPerPixel;
}

double TerrainSurface::sampleZ(int row) const {
    return region.centerZ + (row + 0.5 - region.size / 2.0) * region.metersPerPixel;
}

TerrainSurface surfaceFromGrids(const Heightfield& field, const HeightmapRegion& region,
                                const SurfaceGrids& grids) {
    TerrainSurface surface{region, {}};
    const int size = region.size;
    const double spacing = region.metersPerPixel;
    const std::vector<float> rivers = riverDistances(grids.river, size, spacing);
    surface.sites.resize(rivers.size());
    const auto ground = [&](int column, int row) {
        return grids.ground[surface.index(column, row)];
    };
    for (int row = 0; row < size; ++row) {
        const int north = std::max(row - 1, 0);
        const int south = std::min(row + 1, size - 1);
        for (int column = 0; column < size; ++column) {
            const int west = std::max(column - 1, 0);
            const int east = std::min(column + 1, size - 1);
            const std::size_t i = surface.index(column, row);
            SurfaceSite& site = surface.sites[i];
            site.ground = grids.ground[i];
            const Neighbors neighbors{{ground(east, row), ground(west, row), ground(column, south),
                                       ground(column, north)},
                                      (east - west) * spacing,
                                      (south - north) * spacing};
            setSlopes(site, neighbors, spacing);
            site.riverDistance = rivers[i];
            site.continentalness =
                field.sampleAt(surface.sampleX(column), surface.sampleZ(row)).continentalness;
            site.rockAboveAir = flagged(grids.rockAboveAir, i);
            site.cliffWall = flagged(grids.cliffWall, i);
            setWater(site, flagged(grids.river, i));
        }
    }
    return surface;
}

TerrainSurface surfaceFromHeightfield(const Heightfield& field, const HeightmapRegion& region) {
    TerrainSurface surface{region, {}};
    surface.sites.reserve(static_cast<std::size_t>(region.size) *
                          static_cast<std::size_t>(region.size));
    const double step = region.metersPerPixel;
    const SurfaceRelief relief = surfaceRelief(field, surface);
    for (int row = 0; row < region.size; ++row) {
        for (int column = 0; column < region.size; ++column) {
            const std::size_t i = surface.index(column, row);
            const HeightSample& sample = relief.samples[i];
            SurfaceSite site;
            site.ground = sample.height;
            site.continentalness = sample.continentalness;
            const Neighbors neighbors{relief.neighbors[i], 2.0 * step, 2.0 * step};
            setSlopes(site, neighbors, step);
            setWater(site, false);
            surface.sites.push_back(site);
        }
    }
    return surface;
}

TerrainSurface surfaceFromEroded(const ErodedRegion& eroded) {
    return surfaceFromGrids(eroded.field(), eroded.region(),
                            {.ground = eroded.grid().heights, .river = eroded.rivers().river});
}

TerrainSurface surfaceFromSculpted(const SculptedRegion& sculpted) {
    const CliffRegion& cliffs = sculpted.cliffs();
    const ErodedRegion& eroded = cliffs.eroded();
    const float drop = kWallDropShare * cliffs.field().params().stratumHeight;
    const std::vector<std::uint8_t> walls = cliffWalls(sculpted.surface(), cliffs.mask(), drop);
    return surfaceFromGrids(eroded.field(), eroded.region(),
                            {.ground = sculpted.surface().heights,
                             .river = eroded.rivers().river,
                             .rockAboveAir = sculpted.airBelowRock(),
                             .cliffWall = walls});
}

SurfaceSite heightfieldSite(float ground, float continentalness,
                            const std::array<float, 4>& neighbors, double step) {
    SurfaceSite site;
    site.ground = ground;
    site.continentalness = continentalness;
    setSlopes(site, Neighbors{neighbors, 2.0 * step, 2.0 * step}, step);
    setWater(site, false);
    return site;
}

} // namespace terrain
