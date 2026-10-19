#pragma once

#include "terrain/generation/height_grid.hpp"

#include <cstdint>

namespace terrain {

// Erosion works on slopes (height differences divided by the cell size) and carries
// sediment in proportion to the cell size, so the parameters do not depend on the resolution.
struct ErosionParams {
    // Hydraulic erosion: dropletsPerCell * size^2 droplets, released at deterministic
    // pseudo-random positions, each moving one cell per step for at most maxSteps steps.
    float dropletsPerCell = 1.0f;
    int maxSteps = 64;
    float inertia = 0.1f;          // [0, 1): share of its previous direction a droplet keeps
    float capacityFactor = 1.0f;   // sediment carried per unit of drop, speed and water
    float minCapacitySlope = 0.01f; // keeps some capacity on nearly flat ground
    float erodeRate = 0.3f;        // share of the missing capacity taken from the ground
    float depositRate = 0.3f;      // share of the excess sediment dropped
    float evaporation = 0.02f;     // share of the water lost per step
    float gravity = 4.0f;
    float maxSpeed = 4.0f;         // keeps fast droplets on steep slopes from digging canyons
    int brushRadius = 2;           // cells eroded around a droplet

    // Thermal smoothing: material slides down from slopes steeper than talusSlope (rise over
    // run), moving thermalRate of the excess per iteration.
    int thermalIterations = 20;
    float talusSlope = 1.2f;
    float thermalRate = 0.5f;

    // The change fades to zero over this many cells toward the grid border, whose cells are
    // never modified, so an eroded region joins the surrounding terrain without a seam.
    int borderMargin = 16;
};

// Every function below is deterministic for a given grid, seed and parameters. Ocean cells
// (height < kSeaLevel) are never modified and land cells never go below kSeaLevel, so the
// coastline is preserved exactly.

// Droplets run down the slope, erode the ground where they can carry more sediment and
// deposit it where they slow down or climb. Droplets stop on reaching the ocean, where their
// sediment is lost: erosion never builds new land.
void erodeHydraulic(HeightGrid& grid, std::uint64_t seed, const ErosionParams& params = {});

// Thermal erosion: lowers slopes steeper than the talus slope by moving material to the
// lower land neighbors (8-connected), conserving it on land.
void smoothSteepSlopes(HeightGrid& grid, const ErosionParams& params = {});

// Full erosion of a region: hydraulic erosion, then thermal smoothing, then the change is
// faded out toward the border.
void erodeHeightGrid(HeightGrid& grid, std::uint64_t seed, const ErosionParams& params = {});

// Weight of the erosion at a cell: 0 on the border ring, 1 from borderMargin cells inward,
// smooth in between.
[[nodiscard]] float borderWeight(int column, int row, int size, int borderMargin);

} // namespace terrain
