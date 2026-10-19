#pragma once

#include "terrain/generation/height_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

struct RiverParams {
    // A land cell is a river cell when the area draining through it reaches this, in m^2.
    double minCatchmentArea = 2.0e6;

    // Light carving of the beds: bedDepth meters at minCatchmentArea, plus
    // bedDepthPerDoubling each time the catchment doubles, at most maxBedDepth.
    float bedDepth = 2.0f;
    float bedDepthPerDoubling = 1.0f;
    float maxBedDepth = 10.0f;

    // A cascade is a river cell whose slope (steepest descent, rise over run) is at least
    // cascadeMinSlope and exceeds the slope of its main donor, just upstream, by
    // cascadeMinBreak: a break in the slope of the river, not merely a steep reach.
    float cascadeMinSlope = 0.35f;
    float cascadeMinBreak = 0.25f;
};

inline constexpr int kNoReceiver = -1;

// Waterfall at a break in the slope of a river.
struct Cascade {
    int column = 0;
    int row = 0;
    float top = 0.0f;        // water height at the cascade cell, meters
    float bottom = 0.0f;     // water height at its receiver
    float slopeAbove = 0.0f; // steepest descent slope of the main donor, just upstream
    float slopeBelow = 0.0f; // steepest descent slope of the cascade cell
    double catchment = 0.0;  // m^2

    friend bool operator==(const Cascade&, const Cascade&) = default;
};

// Drainage of a height grid. Cells are indexed like HeightGrid::heights.
struct RiverNetwork {
    int size = 0;
    double metersPerPixel = 1.0;
    // Steepest lower neighbor (8-connected) the water flows to; kNoReceiver at the outlets:
    // ocean cells, the border ring (water leaves the region) and remaining sinks.
    std::vector<int> receiver;
    // Area of the land draining through each cell, the cell included, in m^2.
    std::vector<double> catchment;
    std::vector<std::uint8_t> river; // 1 on river cells (land, catchment >= minimum)
    // Every cell, each one before its receiver: from the sources down to the outlets.
    std::vector<int> downstreamOrder;
    std::vector<Cascade> cascades;

    [[nodiscard]] bool isRiver(int index) const {
        return river[static_cast<std::size_t>(index)] != 0;
    }
    [[nodiscard]] int receiverOf(int index) const {
        return receiver[static_cast<std::size_t>(index)];
    }
};

// Priority-Flood + epsilon (Barnes et al. 2014): raises the land cells of closed depressions
// just enough that every land cell has a strictly lower neighbor, along a path that reaches
// an outlet (an ocean cell or the border ring). Only raises land; ocean and border cells are
// never modified. Deterministic: ties are broken by cell index.
void fillDepressions(HeightGrid& grid);

// Receivers, drainage order, catchment areas and river cells. The receiver of a land cell is
// its steepest strictly lower neighbor, with the diagonal distance randomized per cell from
// seed (Rho8) so that rivers do not run along the 8 grid directions.
// Expects a depression-free grid (see fillDepressions); remaining sinks become outlets.
[[nodiscard]] RiverNetwork computeDrainage(const HeightGrid& grid, std::uint64_t seed,
                                           const RiverParams& params = {});

// Lowers the river cells by their bed depth, from the outlets upstream, keeping every river
// bed at or above its receiver and land at or above sea level: river beds never rise
// downstream. Only river cells with a receiver are modified (never the border ring).
void carveRiverBeds(HeightGrid& grid, const RiverNetwork& network, const RiverParams& params);

// Bed depth of a river cell draining catchment m^2.
[[nodiscard]] float bedDepth(double catchment, const RiverParams& params);

// River cells at a break in slope (see RiverParams), in cell index order. The main donor is
// the donor with the largest catchment; sources (no donor) are never cascades. Slopes are
// measured on the water surface: the ground, and sea level over the ocean.
[[nodiscard]] std::vector<Cascade> detectCascades(const HeightGrid& grid,
                                                  const RiverNetwork& network,
                                                  const RiverParams& params);

// Rivers of a relief, generated after it: fills depressions, computes the drainage, carves
// the beds and detects the cascades on the carved grid.
[[nodiscard]] RiverNetwork generateRivers(HeightGrid& grid, std::uint64_t seed,
                                          const RiverParams& params = {});

} // namespace terrain
