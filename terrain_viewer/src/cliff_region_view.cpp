#include "cliff_region_view.hpp"

#include "terrain/generation/cliff_region.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/rivers.hpp"
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

// The mesh covers the grid of the region: kChunksPerSide chunks of kChunkCells cells of
// kCliffMetersPerUnit meters on each side. A model unit is one cell.
constexpr double kChunkMeters = kCliffMetersPerUnit * terrain::kChunkCells;
constexpr double kRegionMeters = (kCliffGridSize - 1) * kCliffMetersPerPixel;
constexpr int kChunksPerSide = static_cast<int>(kRegionMeters / kChunkMeters);
static_assert(kChunksPerSide * kChunkMeters == kRegionMeters,
              "the cliff region must be a whole number of chunks");
constexpr float kRegionUnits = static_cast<float>(kChunksPerSide * terrain::kChunkCells);

// Model units between two samples of the grids of the region.
constexpr auto kUnitsPerPixel = static_cast<float>(kCliffMetersPerPixel / kCliffMetersPerUnit);

// Rivers are drawn this far above their water and the sea this far below sea level, in model
// units, so that neither fights with the rock (land never goes below sea level).
constexpr float kWaterLift = 0.15f;
constexpr float kSeaDrop = 0.05f;

// Half-width of the river ribbons in grid cells, from streams to rivers draining 2^6 times more.
constexpr float kStreamHalfWidth = 0.25f * kUnitsPerPixel;
constexpr float kRiverHalfWidth = 0.7f * kUnitsPerPixel;
constexpr double kWidthDoublings = 6.0;

// The camera starts above the highest rock, back from the south edge (model units).
constexpr float kViewpointClearance = 40.0f;
constexpr float kViewpointSetback = 120.0f;

// World position of the sample (0, 0) of the grids: the corner of the mesh.
struct MeshOrigin {
    double x;
    double z;
};

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

// Meshes with Marching Cubes the chunks of column (chunkX, chunkZ) between its lowest and
// highest rock surface, and appends them to the view.
void meshColumn(const terrain::CliffRegion& cliffs, MeshOrigin origin, int chunkX, int chunkZ,
                CliffRegionView& view) {
    const terrain::CliffChunkColumns columns = terrain::sampleCliffColumns(
        cliffs, origin.x + chunkX * kChunkMeters, origin.z + chunkZ * kChunkMeters,
        kCliffMetersPerUnit);
    const auto lowest = static_cast<int>(std::floor(columns.minHeight / kChunkMeters));
    const auto highest = static_cast<int>(std::floor(columns.maxHeight / kChunkMeters));
    for (int level = lowest; level <= highest; ++level) {
        const terrain::DensityGrid grid =
            terrain::cliffDensityFromColumns(cliffs.field(), columns, level * kChunkMeters);
        appendMesh(view.terrain, terrain::marchingCubes(grid), chunkX * terrain::kChunkCells,
                   level * terrain::kChunkCells, chunkZ * terrain::kChunkCells);
    }
    constexpr auto metersPerUnit = static_cast<float>(kCliffMetersPerUnit);
    view.minHeight = std::min(view.minHeight, columns.minHeight / metersPerUnit);
    view.maxHeight = std::max(view.maxHeight, columns.maxHeight / metersPerUnit);
}

void appendQuad(std::vector<ColoredVertex>& out, const Vec3 (&corners)[4], Vec3 color) {
    for (const int k : {0, 1, 2, 0, 2, 3}) {
        out.push_back({corners[k], color});
    }
}

// Water surface above a cell: at its water height, just lifted, or at sea level over the ocean.
Vec3 waterPoint(const terrain::HeightGrid& grid, int index) {
    const float ground = grid.heights[static_cast<std::size_t>(index)];
    const float height = std::max(ground, terrain::kSeaLevel);
    return {static_cast<float>(index % grid.size) * kUnitsPerPixel,
            height / static_cast<float>(kCliffMetersPerUnit) + kWaterLift,
            static_cast<float>(index / grid.size) * kUnitsPerPixel};
}

// Half-width of the ribbon of a river cell draining catchment m^2.
float ribbonHalfWidth(double catchment) {
    const double minimum = terrain::RiverParams{}.minCatchmentArea;
    const double doublings = std::log2(std::max(catchment / minimum, 1.0));
    const auto t = static_cast<float>(std::min(doublings / kWidthDoublings, 1.0));
    return kStreamHalfWidth + (kRiverHalfWidth - kStreamHalfWidth) * t;
}

// One ribbon from each river cell to its receiver, white where it is a cascade.
void appendRivers(const terrain::ErodedRegion& eroded, CliffRegionView& view) {
    const terrain::HeightGrid& grid = eroded.grid();
    const terrain::RiverNetwork& rivers = eroded.rivers();
    std::vector<bool> cascade(grid.heights.size(), false);
    for (const terrain::Cascade& c : rivers.cascades) {
        cascade[grid.index(c.column, c.row)] = true;
    }
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const int receiver = rivers.receiverOf(i);
        if (!rivers.isRiver(i) || receiver == terrain::kNoReceiver) {
            continue;
        }
        const Vec3 from = waterPoint(grid, i);
        const Vec3 to = waterPoint(grid, receiver);
        const float halfWidth = ribbonHalfWidth(rivers.catchment[static_cast<std::size_t>(i)]);
        const Vec3 side = normalize(Vec3{-(to.z - from.z), 0.0f, to.x - from.x}) * halfWidth;
        const Vec3 color = cascade[static_cast<std::size_t>(i)] ? kCascadeColor : kRiverColor;
        appendQuad(view.water, {from - side, from + side, to + side, to - side}, color);
        ++view.riverCells;
    }
    view.cascades = rivers.cascades.size();
}

// Flat sea over the whole region when it has an ocean, hidden by the land above sea level.
void appendSea(const terrain::HeightGrid& grid, CliffRegionView& view) {
    const bool hasOcean = std::any_of(grid.heights.begin(), grid.heights.end(),
                                      [](float h) { return h < terrain::kSeaLevel; });
    if (hasOcean) {
        appendQuad(view.water,
                   {{0.0f, -kSeaDrop, 0.0f},
                    {0.0f, -kSeaDrop, kRegionUnits},
                    {kRegionUnits, -kSeaDrop, kRegionUnits},
                    {kRegionUnits, -kSeaDrop, 0.0f}},
                   kSeaColor);
    }
}

} // namespace

CliffRegionView buildCliffRegionView(std::uint64_t seed) {
    const terrain::HeightmapRegion region =
        terrain::findCliffRegion(seed, kCliffMetersPerPixel, kCliffGridSize);
    const terrain::CliffRegion cliffs(seed, region);
    const terrain::ErodedRegion& eroded = cliffs.eroded();
    const MeshOrigin origin{eroded.sampleX(0), eroded.sampleZ(0)};

    CliffRegionView view;
    view.minHeight = std::numeric_limits<float>::max();
    view.maxHeight = std::numeric_limits<float>::lowest();
    for (int chunkZ = 0; chunkZ < kChunksPerSide; ++chunkZ) {
        for (int chunkX = 0; chunkX < kChunksPerSide; ++chunkX) {
            meshColumn(cliffs, origin, chunkX, chunkZ, view);
        }
    }
    appendRivers(eroded, view);
    appendSea(eroded.grid(), view);
    view.cirques = cliffs.cirques().size();
    view.viewpoint = {kRegionUnits / 2.0f, view.maxHeight + kViewpointClearance,
                      kRegionUnits + kViewpointSetback};
    return view;
}

} // namespace viewer
