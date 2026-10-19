#include "sculpted_region_view.hpp"

#include "cliff_region_view.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/rivers.hpp"
#include "terrain/generation/sculpted_region.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {

namespace {

constexpr Vec3 kRiverColor{0.15f, 0.40f, 0.85f};
constexpr Vec3 kCascadeColor{0.95f, 0.97f, 1.0f};
constexpr Vec3 kSeaColor{0.10f, 0.30f, 0.60f};

// The window is kChunksPerSide chunks of kChunkCells cells of kSculptedMetersPerUnit meters
// on each side. A model unit is one cell.
constexpr double kChunkMeters = kSculptedMetersPerUnit * terrain::kChunkCells;
constexpr int kChunksPerSide = static_cast<int>(kSculptedWindowMeters / kChunkMeters);
static_assert(kChunksPerSide * kChunkMeters == kSculptedWindowMeters,
              "the window must be a whole number of chunks");
constexpr float kWindowUnits = static_cast<float>(kChunksPerSide * terrain::kChunkCells);
constexpr auto kUnitsPerMeter = static_cast<float>(1.0 / kSculptedMetersPerUnit);

// Scores of the window: an arch in it weighs as much as kArchScore samples with rock above
// air, an arch cut by its edge kCutArchScore against it.
constexpr int kArchScore = 30;
constexpr int kCutArchScore = 1000;

// Rivers are drawn this far above their water and the sea this far below sea level, in model
// units, so that neither fights with the rock.
constexpr float kWaterLift = 0.15f;
constexpr float kSeaDrop = 0.05f;

// Half-width of the river ribbons in meters, from streams to rivers draining 2^6 times more
// (a quarter and 0.7 of a grid cell, as in the cliff mode).
constexpr float kStreamHalfWidth = 0.25f * static_cast<float>(kCliffMetersPerPixel);
constexpr float kRiverHalfWidth = 0.7f * static_cast<float>(kCliffMetersPerPixel);
constexpr double kWidthDoublings = 6.0;

// The camera starts above the highest rock, back from the south edge (model units).
constexpr float kViewpointClearance = 30.0f;
constexpr float kViewpointSetback = 80.0f;

// A point of the world: the center or the corner of the window.
struct Window {
    double x;
    double z;
};

bool inWindow(const Window& center, double x, double z) {
    const double half = kSculptedWindowMeters / 2.0;
    return std::abs(x - center.x) <= half && std::abs(z - center.z) <= half;
}

// +1 if the arch lies entirely in the window, -1 if the edge of the window cuts it, else 0.
int archInWindow(const terrain::SculptedRegion& region, const Window& center,
                 const terrain::Arch& arch) {
    const double half = kSculptedWindowMeters / 2.0;
    const double radius = region.archField().boundingRadius(arch);
    const double dx = std::abs(arch.x - center.x);
    const double dz = std::abs(arch.z - center.z);
    if (dx + radius <= half && dz + radius <= half) {
        return 1;
    }
    return dx - radius < half && dz - radius < half ? -1 : 0;
}

// Positions of the samples with rock above air.
std::vector<Window> overhangPoints(const terrain::SculptedRegion& region) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    const int size = eroded.grid().size;
    std::vector<Window> points;
    for (int i = 0; i < static_cast<int>(region.airBelowRock().size()); ++i) {
        if (region.airBelowRock()[static_cast<std::size_t>(i)] != 0) {
            points.push_back({eroded.sampleX(i % size), eroded.sampleZ(i / size)});
        }
    }
    return points;
}

// Center of the window, searched every chunk inside the region: the most arches and samples
// with rock above air in it, the fewest arches cut by its edge (the first on ties).
Window chooseWindow(const terrain::SculptedRegion& region) {
    const terrain::HeightmapRegion& area = region.cliffs().eroded().region();
    const std::vector<Window> overhangs = overhangPoints(region);
    const double room = (area.size - 1) * area.metersPerPixel / 2.0 - kSculptedWindowMeters / 2.0;
    const int steps = std::max(0, static_cast<int>(room / kChunkMeters));
    Window best{area.centerX, area.centerZ};
    int bestScore = std::numeric_limits<int>::lowest();
    for (int j = -steps; j <= steps; ++j) {
        for (int i = -steps; i <= steps; ++i) {
            const Window center{area.centerX + i * kChunkMeters, area.centerZ + j * kChunkMeters};
            int score = 0;
            for (const terrain::Arch& arch : region.arches()) {
                const int inside = archInWindow(region, center, arch);
                score += inside > 0 ? kArchScore : (inside < 0 ? -kCutArchScore : 0);
            }
            for (const Window& point : overhangs) {
                score += inWindow(center, point.x, point.z) ? 1 : 0;
            }
            if (score > bestScore) {
                best = center;
                bestScore = score;
            }
        }
    }
    return best;
}

void appendMesh(terrain::Mesh& target, const terrain::Mesh& part, int offsetX, int offsetY,
                int offsetZ) {
    const auto first = static_cast<std::uint32_t>(target.vertices.size());
    for (const terrain::Vec3f& v : part.vertices) {
        target.vertices.push_back({static_cast<float>(offsetX) + v.x,
                                   static_cast<float>(offsetY) + v.y,
                                   static_cast<float>(offsetZ) + v.z});
    }
    target.normals.insert(target.normals.end(), part.normals.begin(), part.normals.end());
    for (const std::uint32_t index : part.indices) {
        target.indices.push_back(first + index);
    }
}

// Meshes the chunks of column (chunkX, chunkZ) of the window between the bounds of its rock
// surfaces, and appends them to the view.
void meshColumn(const terrain::SculptedRegion& region, const Window& corner, int chunkX,
                int chunkZ, SculptedRegionView& view) {
    const terrain::SculptedChunkColumns columns =
        terrain::sampleSculptedColumns(region, corner.x + chunkX * kChunkMeters,
                                       corner.z + chunkZ * kChunkMeters, kSculptedMetersPerUnit);
    const auto lowest = static_cast<int>(std::floor(columns.minHeight / kChunkMeters));
    const auto highest = static_cast<int>(std::floor(columns.maxHeight / kChunkMeters));
    for (int level = lowest; level <= highest; ++level) {
        const terrain::DensityGrid grid =
            terrain::sculptedDensityFromColumns(region, columns, level * kChunkMeters);
        appendMesh(view.terrain, terrain::marchingCubes(grid), chunkX * terrain::kChunkCells,
                   level * terrain::kChunkCells, chunkZ * terrain::kChunkCells);
    }
    view.minHeight = std::min(view.minHeight, columns.minHeight * kUnitsPerMeter);
    view.maxHeight = std::max(view.maxHeight, columns.maxHeight * kUnitsPerMeter);
}

void appendQuad(std::vector<ColoredVertex>& out, const Vec3 (&corners)[4], Vec3 color) {
    for (const int k : {0, 1, 2, 0, 2, 3}) {
        out.push_back({corners[k], color});
    }
}

// Water surface above a cell, in model units from the corner of the window: at its water
// height, just lifted, or at sea level over the ocean.
Vec3 waterPoint(const terrain::ErodedRegion& eroded, const Window& corner, int index) {
    const terrain::HeightGrid& grid = eroded.grid();
    const float height = std::max(grid.heights[static_cast<std::size_t>(index)], terrain::kSeaLevel);
    return {static_cast<float>(eroded.sampleX(index % grid.size) - corner.x) * kUnitsPerMeter,
            height * kUnitsPerMeter + kWaterLift,
            static_cast<float>(eroded.sampleZ(index / grid.size) - corner.z) * kUnitsPerMeter};
}

// Half-width in model units of the ribbon of a river cell draining catchment m^2.
float ribbonHalfWidth(double catchment) {
    const double minimum = terrain::RiverParams{}.minCatchmentArea;
    const double doublings = std::log2(std::max(catchment / minimum, 1.0));
    const auto t = static_cast<float>(std::min(doublings / kWidthDoublings, 1.0));
    return (kStreamHalfWidth + (kRiverHalfWidth - kStreamHalfWidth) * t) * kUnitsPerMeter;
}

// One ribbon from each river cell of the window to its receiver, white where it is a
// cascade, and a flat sea over the window if it holds an ocean.
void appendWater(const terrain::ErodedRegion& eroded, const Window& center, const Window& corner,
                 SculptedRegionView& view) {
    const terrain::HeightGrid& grid = eroded.grid();
    const terrain::RiverNetwork& rivers = eroded.rivers();
    std::vector<bool> cascade(grid.heights.size(), false);
    for (const terrain::Cascade& c : rivers.cascades) {
        cascade[grid.index(c.column, c.row)] = true;
    }
    bool ocean = false;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        if (!inWindow(center, eroded.sampleX(i % grid.size), eroded.sampleZ(i / grid.size))) {
            continue;
        }
        ocean = ocean || grid.heights[static_cast<std::size_t>(i)] < terrain::kSeaLevel;
        const int receiver = rivers.receiverOf(i);
        if (!rivers.isRiver(i) || receiver == terrain::kNoReceiver) {
            continue;
        }
        const Vec3 from = waterPoint(eroded, corner, i);
        const Vec3 to = waterPoint(eroded, corner, receiver);
        const float halfWidth = ribbonHalfWidth(rivers.catchment[static_cast<std::size_t>(i)]);
        const Vec3 side = normalize(Vec3{-(to.z - from.z), 0.0f, to.x - from.x}) * halfWidth;
        const Vec3 color = cascade[static_cast<std::size_t>(i)] ? kCascadeColor : kRiverColor;
        appendQuad(view.water, {from - side, from + side, to + side, to - side}, color);
    }
    if (ocean) {
        appendQuad(view.water,
                   {{0.0f, -kSeaDrop, 0.0f},
                    {0.0f, -kSeaDrop, kWindowUnits},
                    {kWindowUnits, -kSeaDrop, kWindowUnits},
                    {kWindowUnits, -kSeaDrop, 0.0f}},
                   kSeaColor);
    }
}

// Arches and samples with rock above air in the window.
void countFeatures(const terrain::SculptedRegion& region, const Window& center,
                   SculptedRegionView& view) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    const int size = eroded.grid().size;
    view.arches = region.arches().size();
    for (const terrain::Arch& arch : region.arches()) {
        view.windowArches += inWindow(center, arch.x, arch.z) ? 1 : 0;
    }
    for (int i = 0; i < static_cast<int>(region.airBelowRock().size()); ++i) {
        view.overhangSamples += region.airBelowRock()[static_cast<std::size_t>(i)] != 0 &&
                                        inWindow(center, eroded.sampleX(i % size),
                                                 eroded.sampleZ(i / size))
                                    ? 1
                                    : 0;
    }
}

} // namespace

SculptedRegionView buildSculptedRegionView(std::uint64_t seed) {
    const terrain::HeightmapRegion area =
        terrain::findCliffRegion(seed, kCliffMetersPerPixel, kCliffGridSize);
    const terrain::SculptedRegion region(seed, area);
    const Window center = chooseWindow(region);
    const Window corner{center.x - kSculptedWindowMeters / 2.0,
                        center.z - kSculptedWindowMeters / 2.0};

    SculptedRegionView view;
    view.minHeight = std::numeric_limits<float>::max();
    view.maxHeight = std::numeric_limits<float>::lowest();
    for (int chunkZ = 0; chunkZ < kChunksPerSide; ++chunkZ) {
        for (int chunkX = 0; chunkX < kChunksPerSide; ++chunkX) {
            meshColumn(region, corner, chunkX, chunkZ, view);
        }
    }
    appendWater(region.cliffs().eroded(), center, corner, view);
    countFeatures(region, center, view);
    view.viewpoint = {kWindowUnits / 2.0f, view.maxHeight + kViewpointClearance,
                      kWindowUnits + kViewpointSetback};
    return view;
}

} // namespace viewer
