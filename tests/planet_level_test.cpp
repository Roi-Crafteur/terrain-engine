// The planet level on a small world: determinism to the bit whatever the number of threads,
// interpolation, the hydrology invariants and the memory kept.
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_golden_support.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::PlanetBuild;
using terrain::PlanetLevel;

bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    bool same = a.size() == b.size();
    for (std::size_t c = 0; same && c < a.size(); ++c) {
        same = std::bit_cast<std::uint32_t>(a[c]) == std::bit_cast<std::uint32_t>(b[c]);
    }
    return same;
}

void testThreads(const terrain::HeightfieldParams& relief) {
    const PlanetBuild one = golden::smallBuild(relief, 1);
    const PlanetBuild two = golden::smallBuild(relief, 2);
    const PlanetBuild four = golden::smallBuild(relief, 4);
    for (const PlanetBuild* other : {&two, &four}) {
        TEST_CHECK(sameBits(one.height, other->height));
        TEST_CHECK(sameBits(one.climate.precipitation, other->climate.precipitation));
        TEST_CHECK(sameBits(one.discharge, other->discharge));
        TEST_CHECK(one.receiver == other->receiver && one.lakeOfCell == other->lakeOfCell);
    }
    PlanetBuild a = one;
    PlanetBuild b = four;
    TEST_CHECK(PlanetLevel(std::move(a), {}).checksum() ==
               PlanetLevel(std::move(b), {}).checksum());
    // The same seed again: the same level.
    const PlanetLevel again(terrain::kDefaultWorldSeed, relief, golden::smallPlanetParams(3),
                            golden::kSmallPlanet);
    PlanetBuild c = one;
    TEST_CHECK(again.checksum() == PlanetLevel(std::move(c), {}).checksum());
}

void testInvariants(const char* name, const terrain::HeightfieldParams& relief) {
    PlanetBuild build = golden::smallBuild(relief, 2);
    // Every unit of runoff reaches the sea or evaporates from a lake.
    TEST_CHECK(build.budget.runoff > 0);
    TEST_CHECK(build.budget.runoff == build.budget.toOcean + build.budget.lakeEvaporation);
    for (const terrain::PlanetLake& lake : build.lakes) {
        TEST_CHECK(lake.inflow == lake.evaporation + lake.outflow);
    }
    // The water always goes down or into a lake: along every river.
    bool downhill = true;
    for (const terrain::RiverSegment& s : build.rivers.segments) {
        for (std::size_t v = 1; v < s.vertices.size(); ++v) {
            downhill = downhill && s.vertices[v].elevation <= s.vertices[v - 1].elevation;
        }
    }
    TEST_CHECK(downhill);
    const std::size_t cells = build.grid.cells();
    std::size_t land = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        land += build.ocean[c] == terrain::planet::kLand ? 1u : 0u;
    }
    const PlanetLevel level(std::move(build), {});
    // Every land cell ends at the sea, a lake or a closed basin.
    const std::vector<std::int32_t> basins = level.basins();
    bool ends = true;
    for (std::size_t c = 0; c < cells; ++c) {
        ends = ends &&
               (level.cells().ocean(c) ? basins[c] == terrain::planet::kNoBasin : basins[c] >= 0);
    }
    TEST_CHECK(ends);
    std::printf(
        "%s: %zu x %zu cells, %.0f%% land, %zu lakes, %zu river segments, %zu features, "
        "%zu bytes kept\n",
        name, static_cast<std::size_t>(level.grid().nx), static_cast<std::size_t>(level.grid().nz),
        100.0 * static_cast<double>(land) / static_cast<double>(cells), level.lakes().size(),
        level.rivers().segments.size(), level.features().size(), level.memoryBytes());
    TEST_CHECK(level.memoryBytes() < 8 * cells + 1'000'000);
}

void testSampleAt() {
    const PlanetLevel level(terrain::kDefaultWorldSeed, golden::smallPlanetaryParams(),
                            golden::smallPlanetParams(2), golden::kSmallPlanet);
    const terrain::PlanetGrid& grid = level.grid();
    // At a cell center, the values of the cell.
    const std::size_t cell = grid.index(100, 40);
    const terrain::PlanetSample center = level.sampleAt(grid.centerX(100), grid.centerZ(40));
    TEST_CHECK(center.elevation == level.elevation(cell));
    TEST_CHECK(center.precipitation == level.precipitation(cell));
    TEST_CHECK(center.temperature == level.temperature(cell));
    TEST_CHECK(center.ocean == level.cells().ocean(cell) &&
               center.lake == level.cells().lakeAt(cell));
    // Halfway between two centers, their mean.
    const std::size_t next = grid.index(101, 40);
    const terrain::PlanetSample half =
        level.sampleAt(grid.centerX(100) + 0.5 * grid.cellSize, grid.centerZ(40));
    TEST_CHECK(half.elevation ==
               static_cast<float>(
                   0.5 * (static_cast<double>(level.elevation(cell)) + level.elevation(next))));
    // Every image of a point gives the same sample, across both seams.
    const double x = 0.5 * grid.periodX() - 1000.0;
    const double z = -0.5 * grid.periodZ() + 300.0;
    const terrain::PlanetSample a = level.sampleAt(x, z);
    const terrain::PlanetSample b = level.sampleAt(x - 3.0 * grid.periodX(), z + grid.periodZ());
    TEST_CHECK(std::bit_cast<std::uint32_t>(a.elevation) ==
               std::bit_cast<std::uint32_t>(b.elevation));
    TEST_CHECK(std::bit_cast<std::uint32_t>(a.precipitation) ==
               std::bit_cast<std::uint32_t>(b.precipitation));
    TEST_CHECK(a.lake == b.lake && a.ocean == b.ocean && a.strahler == b.strahler);
    // Over a river cell, its discharge and order.
    if (!level.cells().riverCells.empty()) {
        const auto& river = level.cells().riverCells.front();
        const terrain::PlanetSample r = level.sampleAt(grid.centerX(grid.column(river.cell)),
                                                       grid.centerZ(grid.row(river.cell)));
        const terrain::RiverSegment& segment =
            level.rivers().segments[static_cast<std::size_t>(river.segment)];
        TEST_CHECK(r.strahler == segment.strahler &&
                   r.discharge ==
                       segment.vertices[static_cast<std::size_t>(river.vertex)].discharge);
    }
}

} // namespace

int main() {
    testThreads(terrain::HeightfieldParams{});
    testThreads(golden::smallPlanetaryParams());
    testInvariants("default relief", terrain::HeightfieldParams{});
    testInvariants("planetary relief", golden::smallPlanetaryParams());
    testSampleAt();
    return test::result();
}
