#include "eroded_region_view.hpp"

#include "heightfield_region.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/heightfield_density.hpp"
#include "terrain/meshing/density_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {

namespace {

constexpr Vec3 kRiverColor{0.15f, 0.40f, 0.85f};
constexpr Vec3 kCascadeColor{0.95f, 0.97f, 1.0f};
constexpr Vec3 kSeaColor{0.10f, 0.30f, 0.60f};

// Rivers are drawn this far above their beds and the sea this far below sea level, in model
// units, so that neither fights with the ground (land never goes below sea level).
constexpr float kWaterLift = 0.1f;
constexpr float kSeaDrop = 0.02f;

// Half-width of the river ribbons in cells, from streams to rivers draining 2^6 times more.
constexpr float kStreamHalfWidth = 0.25f;
constexpr float kRiverHalfWidth = 0.7f;
constexpr double kWidthDoublings = 6.0;

// The camera starts above the highest ground, back from the south edge (model units).
constexpr float kViewpointClearance = 10.0f;
constexpr float kViewpointSetback = 40.0f;

// Heights of the sample columns of chunk (chunkX, chunkZ), in model units.
terrain::ChunkColumns chunkColumns(const terrain::HeightGrid& grid, int chunkX, int chunkZ) {
    const auto scale = static_cast<float>(1.0 / grid.metersPerPixel);
    terrain::ChunkColumns columns;
    constexpr int kSamples = terrain::kGridSamples * terrain::kGridSamples;
    columns.heights.reserve(static_cast<std::size_t>(kSamples));
    for (int z = 0; z < terrain::kGridSamples; ++z) {
        for (int x = 0; x < terrain::kGridSamples; ++x) {
            const int column = chunkX * terrain::kChunkCells + x;
            const int row = chunkZ * terrain::kChunkCells + z;
            columns.heights.push_back(grid.at(column, row) * scale);
        }
    }
    const auto [low, high] = std::minmax_element(columns.heights.begin(), columns.heights.end());
    columns.minHeight = *low;
    columns.maxHeight = *high;
    return columns;
}

void appendQuad(std::vector<ColoredVertex>& out, const Vec3 (&corners)[4], Vec3 color) {
    for (const int k : {0, 1, 2, 0, 2, 3}) {
        out.push_back({corners[k], color});
    }
}

// Water surface above a cell: just above the ground, or at sea level over the ocean.
Vec3 waterPoint(const terrain::HeightGrid& grid, int index) {
    const float ground = grid.heights[static_cast<std::size_t>(index)];
    const float height = std::max(ground, terrain::kSeaLevel);
    return {static_cast<float>(index % grid.size),
            height / static_cast<float>(grid.metersPerPixel) + kWaterLift,
            static_cast<float>(index / grid.size)};
}

// One ribbon from each river cell to its receiver, white where it is a cascade.
void appendRivers(const terrain::ErodedRegion& eroded, ErodedRegionView& view) {
    const terrain::HeightGrid& grid = eroded.grid();
    const terrain::RiverNetwork& rivers = eroded.rivers();
    std::vector<bool> cascade(grid.heights.size(), false);
    for (const terrain::Cascade& c : rivers.cascades) {
        cascade[grid.index(c.column, c.row)] = true;
    }
    const double minimum = terrain::RiverParams{}.minCatchmentArea;
    for (int i = 0; i < static_cast<int>(grid.heights.size()); ++i) {
        const int receiver = rivers.receiverOf(i);
        if (!rivers.isRiver(i) || receiver == terrain::kNoReceiver) {
            continue;
        }
        const Vec3 from = waterPoint(grid, i);
        const Vec3 to = waterPoint(grid, receiver);
        const double doublings =
            std::log2(std::max(rivers.catchment[static_cast<std::size_t>(i)] / minimum, 1.0));
        const auto t = static_cast<float>(std::min(doublings / kWidthDoublings, 1.0));
        const float halfWidth = kStreamHalfWidth + (kRiverHalfWidth - kStreamHalfWidth) * t;
        const Vec3 side = normalize(Vec3{-(to.z - from.z), 0.0f, to.x - from.x}) * halfWidth;
        const Vec3 color = cascade[static_cast<std::size_t>(i)] ? kCascadeColor : kRiverColor;
        appendQuad(view.water, {from - side, from + side, to + side, to - side}, color);
        ++view.riverCells;
    }
    view.cascades = rivers.cascades.size();
}

// Flat sea over the whole region, hidden by the land above sea level.
void appendSea(const terrain::HeightGrid& grid, ErodedRegionView& view) {
    const bool hasOcean = std::any_of(grid.heights.begin(), grid.heights.end(),
                                      [](float h) { return h < terrain::kSeaLevel; });
    if (hasOcean) {
        const auto last = static_cast<float>(grid.size - 1);
        appendQuad(view.water,
                   {{0.0f, -kSeaDrop, 0.0f}, {0.0f, -kSeaDrop, last}, {last, -kSeaDrop, last},
                    {last, -kSeaDrop, 0.0f}},
                   kSeaColor);
    }
}

} // namespace

ErodedRegionView buildErodedRegionView(std::uint64_t seed) {
    const GroundPoint summit = findSummit(terrain::Heightfield(seed));
    const int size = kErodedChunks * terrain::kChunkCells + 1;
    const terrain::HeightmapRegion region{summit.x, summit.z, kErodedMetersPerCell, size};
    const terrain::ErodedRegion eroded(seed, region);

    ErodedRegionView view;
    view.minHeight = std::numeric_limits<float>::max();
    view.maxHeight = std::numeric_limits<float>::lowest();
    for (int chunkZ = 0; chunkZ < kErodedChunks; ++chunkZ) {
        for (int chunkX = 0; chunkX < kErodedChunks; ++chunkX) {
            const terrain::ChunkColumns columns = chunkColumns(eroded.grid(), chunkX, chunkZ);
            meshChunkColumn(columns, chunkX * terrain::kChunkCells, chunkZ * terrain::kChunkCells,
                            view.terrain);
            view.minHeight = std::min(view.minHeight, columns.minHeight);
            view.maxHeight = std::max(view.maxHeight, columns.maxHeight);
        }
    }
    appendRivers(eroded, view);
    appendSea(eroded.grid(), view);
    const float center = static_cast<float>(size - 1) / 2.0f;
    view.viewpoint = {center, view.maxHeight + kViewpointClearance,
                      static_cast<float>(size - 1) + kViewpointSetback};
    return view;
}

} // namespace viewer
