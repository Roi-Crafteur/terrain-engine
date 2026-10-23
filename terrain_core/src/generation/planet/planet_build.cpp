#include "terrain/generation/planet/planet_build.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_hierarchy.hpp"
#include "terrain/generation/planet/planet_order.hpp"
#include "terrain/generation/planet/planet_passes.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <array>
#include <chrono>
#include <cmath>
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

// The points of the rivers moved from the centers of their cells to the floors (the images kept:
// a polyline never jumps by a period), and the lengths of the segments measured again.
void moveToFloors(RiverGraph& graph, const PlanetGrid& grid, const std::vector<std::uint8_t>& floor,
                  int floorSamples) {
    for (RiverSegment& segment : graph.segments) {
        segment.length = 0.0;
        for (std::size_t k = 0; k < segment.vertices.size(); ++k) {
            RiverVertex& v = segment.vertices[k];
            const std::array<double, 2> p = floorPoint(grid, v.cell, floor[v.cell], floorSamples);
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

// Lakes, discharge, rivers and features, once the relief, the ocean and the climate are known.
void buildHydrology(PlanetBuild& build, const PlanetParams& params, int threads, Stopwatch& watch) {
    const PlanetGrid& grid = build.grid;
    const WaterOf water =
        climateWater(grid, build.height, build.ocean, build.climate, params.climate);
    const std::vector<std::uint32_t> order = sortCells(build.height, threads);
    build.receiver = computeReceivers(grid, build.height, build.ocean, threads);
    build.timings.order += watch.lap();
    LakeSolution lakes;
    {
        const DepressionHierarchy hierarchy =
            buildHierarchy(grid, build.height, build.ocean, order, build.receiver);
        build.timings.hierarchy += watch.lap();
        lakes = solveLakes(grid, build.height, hierarchy, order, water);
    }
    build.timings.lakes += watch.lap();
    PlanetFlow flow = routeFlow(grid, build.ocean, build.receiver, order, lakes, water);
    build.budget.runoff = lakes.runoff;
    build.budget.toOcean = flow.toOcean;
    for (const PlanetLake& lake : lakes.lakes) {
        build.budget.lakeEvaporation += lake.evaporation;
    }
    build.timings.flow += watch.lap();
    build.rivers = buildRiverGraph(grid, build.height, build.ocean, build.receiver, order, lakes,
                                   flow, water, params.rivers);
    if (!build.floor.empty()) {
        moveToFloors(build.rivers, grid, build.floor, params.floorSamples);
    }
    build.timings.rivers += watch.lap();
    build.features =
        findRiverFeatures(grid, build.height, build.ocean, build.rivers, params.features);
    build.timings.features += watch.lap();
    build.lakes = std::move(lakes.lakes);
    build.lakeOfCell = std::move(lakes.lakeOfCell);
    build.discharge = std::move(flow.discharge);
}

// The links of the rivers of a build between the floors of their cells (the image of the second
// next to the first): every river cell to the next point of its segment, every lake spilling into
// a river from its pass to its partner.
std::vector<PlanetLink> riverLinks(const PlanetBuild& b, const PlanetParams& params) {
    const PlanetGrid& g = b.grid;
    std::vector<PlanetLink> links;
    const auto add = [&](std::uint32_t from, std::uint32_t to) {
        const std::array<double, 2> a = floorPoint(g, from, b.floor[from], params.floorSamples);
        const std::array<double, 2> c = floorPoint(g, to, b.floor[to], params.floorSamples);
        links.push_back({from, to, a[0], a[1], a[0] + wrapCoordinate(c[0] - a[0], g.periodX()),
                         a[1] + wrapCoordinate(c[1] - a[1], g.periodZ())});
    };
    for (const RiverSegment& s : b.rivers.segments) {
        for (std::size_t v = 0; v + 1 < s.vertices.size(); ++v) {
            add(s.vertices[v].cell, s.vertices[v + 1].cell);
        }
    }
    for (const PlanetLake& lake : b.lakes) {
        if (lake.overflowing && lake.pass != kNoCell && lake.partner != kNoCell) {
            add(lake.pass, lake.partner);
        }
    }
    return links;
}

// The sills of the fine relief (planet_passes.hpp): the cells above them raised, then the lakes
// and the rivers found again, until no link rises above its cut.
void raisePasses(const Heightfield& field, PlanetBuild& build, const PlanetParams& params,
                 int threads, Stopwatch& watch) {
    std::vector<std::pair<std::uint64_t, float>> crossings;
    for (int round = 0; round < params.passes.iterations; ++round) {
        const std::vector<PlanetLink> links = riverLinks(build, params);
        const std::size_t raised = raiseSills(field, build.grid, links, build.mountain,
                                              params.passes, threads, build.height, crossings,
                                              build.passes);
        build.timings.passes += watch.lap();
        if (raised == 0) {
            break;
        }
        ++build.passes.rounds;
        // The lakes and rivers of the last round go before the next ones are built.
        build.receiver = std::vector<std::uint8_t>{};
        build.lakeOfCell = std::vector<std::int32_t>{};
        build.discharge = std::vector<float>{};
        build.lakes = std::vector<PlanetLake>{};
        build.rivers = RiverGraph{};
        build.features = std::vector<RiverFeature>{};
        buildHydrology(build, params, threads, watch);
    }
}

} // namespace

PlanetBuild buildPlanet(const Heightfield& field, const PlanetParams& params) {
    const int threads = resolveThreads(params.threads);
    Stopwatch watch;
    PlanetBuild build;
    build.grid = makePlanetGrid(field.world(), params.cellSize);
    {
        CellRelief cells = sampleCells(field, build.grid, params.samplesPerAxis, threads,
                                       params.floorSamples);
        build.height = std::move(cells.height);
        build.floor = std::move(cells.floor);
        build.mountain = std::move(cells.mountain);
        build.timings.relief = watch.lap();
        // The sea where the field puts it: with the geography, a dry depression below the sea
        // level is land.
        build.ocean = markOcean(build.grid, build.height, cells.sea, params.minOceanArea);
        build.timings.ocean = watch.lap();
    }
    if (params.breachSills) {
        build.breach =
            breachDepressions(build.grid, build.height, build.ocean, build.mountain, params.breach);
        build.timings.breach = watch.lap();
    }
    build.climate = computeClimate(build.grid, build.height, build.ocean, params.climate, threads);
    build.timings.climate = watch.lap();
    buildHydrology(build, params, threads, watch);
    if (params.passes.enabled && params.floorSamples > 1) {
        raisePasses(field, build, params, threads, watch);
    }
    return build;
}

} // namespace terrain::planet
