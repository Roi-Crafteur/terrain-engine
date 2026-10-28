#include "terrain/generation/planet/planet_build.hpp"

#include "planet_parallel.hpp"
#include "planet_steps.hpp"
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_hierarchy.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_order.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace terrain::planet {

namespace {

// Lakes, discharge, rivers and features, once the relief, the ocean and the climate are known;
// the arrays of the land spread over every cell for the build.
void buildHydrology(PlanetBuild& build, const PlanetParams& params, int threads, Stopwatch& watch) {
    const PlanetGrid& grid = build.grid;
    const auto land = std::make_shared<const LandIndex>(build.ocean);
    const WaterOf water =
        climateWater(grid, build.height, build.ocean, build.climate, params.climate);
    const std::vector<std::uint32_t> order = sortLandCells(build.height, *land, threads);
    const LandValues<std::uint8_t> receiver = computeReceivers(grid, build.height, land, threads);
    build.timings.order += watch.lap("order");
    LakeSolution lakes;
    {
        const DepressionHierarchy hierarchy =
            buildHierarchy(grid, build.height, land, order, receiver);
        build.timings.hierarchy += watch.lap("hierarchy");
        lakes = solveLakes(grid, build.height, hierarchy, order, water);
    }
    build.timings.lakes += watch.lap("lakes");
    PlanetFlow flow = routeFlow(grid, build.height, land, receiver, order, lakes, water);
    build.budget.runoff = lakes.runoff;
    build.budget.toOcean = flow.toOcean;
    for (const PlanetLake& lake : lakes.lakes) {
        build.budget.lakeEvaporation += lake.evaporation;
    }
    build.timings.flow += watch.lap("flow");
    build.rivers = buildRiverGraph(grid, build.height, land, receiver, order, lakes, flow, water,
                                   params.rivers);
    if (!build.floor.empty()) {
        moveToFloors(build.rivers, grid, [&build](std::size_t c) { return build.floor[c]; },
                     params.floorSamples);
    }
    build.timings.rivers += watch.lap("rivers");
    build.features = findRiverFeatures(grid, build.height, *land, build.rivers, params.features);
    build.timings.features += watch.lap("features");
    build.receiver = receiver.expand();
    build.lakes = std::move(lakes.lakes);
    build.lakeOfCell = lakes.lakeOfCell.expand();
    build.discharge = flow.discharge.expand();
}

} // namespace

void moveToFloors(RiverGraph& graph, const PlanetGrid& grid,
                  const std::function<std::uint8_t(std::size_t)>& floor, int floorSamples) {
    for (RiverSegment& segment : graph.segments) {
        segment.length = 0.0;
        for (std::size_t k = 0; k < segment.vertices.size(); ++k) {
            RiverVertex& v = segment.vertices[k];
            const std::array<double, 2> p = floorPoint(grid, v.cell, floor(v.cell), floorSamples);
            v.x += p[0] - grid.centerX(grid.column(v.cell));
            v.z += p[1] - grid.centerZ(grid.row(v.cell));
            if (k > 0) {
                const RiverVertex& before = segment.vertices[k - 1];
                segment.length += std::sqrt((v.x - before.x) * (v.x - before.x) +
                                            (v.z - before.z) * (v.z - before.z));
            }
        }
    }
}

PlanetBuild buildPlanet(const Heightfield& field, const PlanetParams& params,
                        const PlanetStageProbe& probe) {
    const int threads = resolveThreads(params.threads);
    Stopwatch watch(probe);
    PlanetBuild build;
    build.grid = makePlanetGrid(field.world(), params.cellSize);
    {
        CellRelief cells = sampleCells(field, build.grid, params.samplesPerAxis, threads,
                                       params.floorSamples);
        build.height = std::move(cells.height);
        build.floor = std::move(cells.floor);
        build.mountain = std::move(cells.mountain);
        build.timings.relief = watch.lap("relief");
        // The sea where the field puts it: with the geography, a dry depression below the sea
        // level is land.
        build.ocean = markOcean(build.grid, build.height, cells.sea, params.minOceanArea);
        build.timings.ocean = watch.lap("ocean");
    }
    if (params.breachSills) {
        build.breach =
            breachDepressions(build.grid, build.height, build.ocean, build.mountain, params.breach);
        build.timings.breach = watch.lap("breach");
    }
    build.climate = computeClimate(build.grid, build.height, build.ocean, params.climate, threads);
    build.timings.climate = watch.lap("climate");
    buildHydrology(build, params, threads, watch);
    return build;
}

} // namespace terrain::planet
