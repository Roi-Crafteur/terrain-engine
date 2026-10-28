// The planet level built in little memory (planet::buildLevelParts, phase 11D).
#include "terrain/generation/planet/planet_level.hpp"

#include "planet_parallel.hpp"
#include "planet_steps.hpp"
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_hierarchy.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_order.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <memory>
#include <utility>

namespace terrain::planet {

namespace {

// What lives from a stage to the next: the relief, the land, the floors of the cells (of the land
// by slot, of the few cells of the sea that have one apart) and the climate (the precipitation of
// the land in float by slot, that of the sea rounded as the level keeps it, by sea slot).
struct Lean {
    PlanetGrid grid;
    std::vector<float> height;
    std::shared_ptr<const LandIndex> land;
    LandValues<std::uint8_t> landFloor;
    SparseCells<std::uint8_t> seaFloor;
    std::vector<float> rowTemperature;
    LandValues<float> landRain;
    std::vector<std::uint16_t> seaRain;
};

void keepFloors(Lean& lean, const std::vector<std::uint8_t>& floor) {
    lean.landFloor = LandValues<std::uint8_t>(lean.land, kCenterFloor, kCenterFloor);
    lean.seaFloor = SparseCells<std::uint8_t>(floor.size(), kCenterFloor);
    for (std::size_t c = 0; c < floor.size(); ++c) {
        if (lean.land->land(c)) {
            lean.landFloor.at(c) = floor[c];
        } else if (floor[c] != kCenterFloor) {
            lean.seaFloor.add(c, floor[c]);
        }
    }
    lean.seaFloor.finish();
}

void keepRain(Lean& lean, PlanetClimate&& climate) {
    lean.landRain = LandValues<float>(lean.land, 0.0f, 0.0f);
    lean.seaRain.assign(lean.grid.cells() - lean.land->landCells(), 0);
    for (std::size_t c = 0; c < lean.grid.cells(); ++c) {
        if (lean.land->land(c)) {
            lean.landRain.at(c) = climate.precipitation[c];
        } else {
            lean.seaRain[lean.land->seaSlot(c)] = roundedPrecipitation(climate.precipitation[c]);
        }
    }
    lean.rowTemperature = std::move(climate.rowTemperature);
}

// The relief, the ocean, the breach and the climate, as buildPlanet does them.
void reliefAndClimate(Lean& lean, PlanetLevel::Parts& parts, const Heightfield& field, int threads,
                      Stopwatch& watch) {
    const PlanetParams& params = parts.params;
    CellRelief cells =
        sampleCells(field, lean.grid, params.samplesPerAxis, threads, params.floorSamples);
    lean.height = std::move(cells.height);
    parts.timings.relief = watch.lap("relief");
    std::vector<std::uint8_t> ocean =
        markOcean(lean.grid, lean.height, cells.sea, params.minOceanArea);
    freeVector(cells.sea);
    lean.land = std::make_shared<const LandIndex>(ocean);
    keepFloors(lean, cells.floor);
    freeVector(cells.floor);
    parts.timings.ocean = watch.lap("ocean");
    if (params.breachSills) {
        parts.breach =
            breachDepressions(lean.grid, lean.height, ocean, cells.mountain, params.breach);
        parts.timings.breach = watch.lap("breach");
    }
    freeVector(cells.mountain);
    freeVector(ocean); // the land index answers from now on
    keepRain(lean, computeClimate(lean.grid, lean.height, *lean.land, params.climate, threads));
    parts.timings.climate = watch.lap("climate");
}

// The compact cells, each full array freed once its compact one is made.
PlanetCells compactLean(Lean& lean, std::vector<std::uint16_t>& landRain,
                        LandValues<std::uint8_t>& receiver, SparseCells<std::int32_t>& lakeOfCell,
                        const RiverGraph& rivers) {
    const LandIndex& land = *lean.land;
    const std::size_t count = lean.grid.cells();
    PlanetCells cells;
    cells.grid = lean.grid;
    cells.rowTemperature = lean.rowTemperature;
    cells.elevation.resize(count);
    for (std::size_t c = 0; c < count; ++c) {
        cells.elevation[c] = roundedElevation(lean.height[c]);
    }
    freeVector(lean.height);
    cells.precipitation.resize(count);
    for (std::size_t c = 0; c < count; ++c) {
        cells.precipitation[c] =
            land.land(c) ? landRain[land.slot(c)] : lean.seaRain[land.seaSlot(c)];
    }
    freeVector(landRain);
    freeVector(lean.seaRain);
    cells.bits.resize(count);
    for (std::size_t c = 0; c < count; ++c) {
        cells.bits[c] =
            static_cast<std::uint8_t>(receiver[c] | (land.land(c) ? 0U : PlanetCells::kOceanBit) |
                                      (lakeOfCell.has(c) ? PlanetCells::kLakeBit : 0U));
    }
    receiver.release();
    cells.lakeCells.reserve(lakeOfCell.entries().size());
    for (const auto& [cell, lake] : lakeOfCell.entries()) {
        cells.lakeCells.push_back({cell, lake});
    }
    lakeOfCell.release();
    cells.riverCells = riverCellsOf(rivers);
    for (const PlanetCells::RiverCell& river : cells.riverCells) {
        cells.bits[river.cell] |= PlanetCells::kRiverBit;
    }
    return cells;
}

// The rivers, their floors and their features, then the compact cells.
void riversAndCells(Lean& lean, PlanetLevel::Parts& parts, LandValues<std::uint8_t>& receiver,
                    std::vector<std::uint32_t>& order, LakeSolution& lakes, PlanetFlow& flow,
                    Stopwatch& watch) {
    const PlanetParams& params = parts.params;
    // The precipitation of the land as the level keeps it: the float is read no more, the rivers
    // only ask whether a cell is frozen.
    std::vector<std::uint16_t> landRain(lean.landRain.values().size());
    for (std::size_t s = 0; s < landRain.size(); ++s) {
        landRain[s] = roundedPrecipitation(lean.landRain.values()[s]);
    }
    lean.landRain.release();
    const WaterOf frozen = [&lean, &params](std::size_t c) {
        const float row = lean.rowTemperature[static_cast<std::size_t>(lean.grid.row(c))];
        return CellWater{0, 0, cellFrozen(row, lean.height[c], params.climate)};
    };
    const RiverRelease release{[&flow] { flow.discharge.release(); },
                               [&order] { freeVector(order); }};
    parts.rivers = buildRiverGraph(lean.grid, lean.height, lean.land, receiver, order, lakes, flow,
                                   frozen, params.rivers, release);
    moveToFloors(
        parts.rivers, lean.grid,
        [&lean](std::size_t c) {
            return lean.land->land(c) ? lean.landFloor[c] : lean.seaFloor[c];
        },
        params.floorSamples);
    lean.landFloor.release();
    lean.seaFloor.release();
    parts.timings.rivers += watch.lap("rivers");
    parts.features =
        findRiverFeatures(lean.grid, lean.height, *lean.land, parts.rivers, params.features);
    parts.timings.features += watch.lap("features");
    parts.cells = compactLean(lean, landRain, receiver, lakes.lakeOfCell, parts.rivers);
    parts.lakes = std::move(lakes.lakes);
}

// Lakes, discharge, rivers and features, then the compact cells.
void hydrology(Lean& lean, PlanetLevel::Parts& parts, int threads, Stopwatch& watch) {
    const PlanetGrid& grid = lean.grid;
    std::vector<std::uint32_t> order = sortLandCells(lean.height, *lean.land, threads);
    LandValues<std::uint8_t> receiver = computeReceivers(grid, lean.height, lean.land, threads);
    parts.timings.order += watch.lap("order");
    const WaterOf water =
        landWater(grid, lean.height, lean.landRain, lean.rowTemperature, parts.params.climate);
    LakeSolution lakes;
    {
        DepressionHierarchy hierarchy =
            buildHierarchy(grid, lean.height, lean.land, order, receiver);
        parts.timings.hierarchy += watch.lap("hierarchy");
        const LakeWater gathered = gatherWater(hierarchy, water);
        hierarchy.label.release();
        lakes = solveLakes(grid, lean.height, hierarchy, order, water, gathered);
    }
    parts.timings.lakes += watch.lap("lakes");
    PlanetFlow flow = routeFlow(grid, lean.height, lean.land, receiver, order, lakes, water);
    parts.budget.runoff = lakes.runoff;
    parts.budget.toOcean = flow.toOcean;
    for (const PlanetLake& lake : lakes.lakes) {
        parts.budget.lakeEvaporation += lake.evaporation;
    }
    parts.timings.flow += watch.lap("flow");
    riversAndCells(lean, parts, receiver, order, lakes, flow, watch);
}

} // namespace

PlanetLevel::Parts buildLevelParts(const Heightfield& field, const PlanetParams& params,
                                   const PlanetStageProbe& probe) {
    const int threads = resolveThreads(params.threads);
    Stopwatch watch(probe);
    Lean lean;
    lean.grid = makePlanetGrid(field.world(), params.cellSize);
    PlanetLevel::Parts parts;
    parts.params = params;
    reliefAndClimate(lean, parts, field, threads, watch);
    hydrology(lean, parts, threads, watch);
    watch.tell("level");
    return parts;
}

} // namespace terrain::planet
