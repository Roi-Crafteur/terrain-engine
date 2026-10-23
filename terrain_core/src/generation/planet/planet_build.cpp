#include "terrain/generation/planet/planet_build.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_hierarchy.hpp"
#include "terrain/generation/planet/planet_order.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <chrono>
#include <utility>

namespace terrain::planet {

namespace {

class Stopwatch {
public:
    double lap() {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - start_).count();
        start_ = now;
        return seconds;
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// Lakes, discharge, rivers and features, once the relief, the ocean and the climate are known.
void buildHydrology(PlanetBuild& build, const PlanetParams& params, int threads, Stopwatch& watch) {
    const PlanetGrid& grid = build.grid;
    const WaterOf water =
        climateWater(grid, build.height, build.ocean, build.climate, params.climate);
    const std::vector<std::uint32_t> order = sortCells(build.height, threads);
    build.receiver = computeReceivers(grid, build.height, build.ocean, threads);
    build.timings.order = watch.lap();
    LakeSolution lakes;
    {
        const DepressionHierarchy hierarchy =
            buildHierarchy(grid, build.height, build.ocean, order, build.receiver);
        build.timings.hierarchy = watch.lap();
        lakes = solveLakes(grid, build.height, hierarchy, order, water);
    }
    build.timings.lakes = watch.lap();
    PlanetFlow flow = routeFlow(grid, build.ocean, build.receiver, order, lakes, water);
    build.budget.runoff = lakes.runoff;
    build.budget.toOcean = flow.toOcean;
    for (const PlanetLake& lake : lakes.lakes) {
        build.budget.lakeEvaporation += lake.evaporation;
    }
    build.timings.flow = watch.lap();
    build.rivers = buildRiverGraph(grid, build.height, build.ocean, build.receiver, order, lakes,
                                   flow, water, params.rivers);
    build.timings.rivers = watch.lap();
    build.features =
        findRiverFeatures(grid, build.height, build.ocean, build.rivers, params.features);
    build.timings.features = watch.lap();
    build.lakes = std::move(lakes.lakes);
    build.lakeOfCell = std::move(lakes.lakeOfCell);
    build.discharge = std::move(flow.discharge);
}

} // namespace

PlanetBuild buildPlanet(const Heightfield& field, const PlanetParams& params) {
    const int threads = resolveThreads(params.threads);
    Stopwatch watch;
    PlanetBuild build;
    build.grid = makePlanetGrid(field.world(), params.cellSize);
    CellRelief cells = sampleCells(field, build.grid, params.samplesPerAxis, threads);
    build.height = std::move(cells.height);
    build.timings.relief = watch.lap();
    // The sea where the field puts it: with the geography, a dry depression below the sea level
    // is land.
    build.ocean = markOcean(build.grid, build.height, cells.sea, params.minOceanArea);
    build.timings.ocean = watch.lap();
    build.climate = computeClimate(build.grid, build.height, build.ocean, params.climate, threads);
    build.timings.climate = watch.lap();
    buildHydrology(build, params, threads, watch);
    return build;
}

} // namespace terrain::planet
