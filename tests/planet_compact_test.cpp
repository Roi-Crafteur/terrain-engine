// The compact form kept by PlanetLevel: altitude to the meter, precipitation to the mm/yr, the
// receivers, the sparse lakes and rivers, and what is recomputed from them.
#include "terrain/generation/planet/planet_flow.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using terrain::PlanetBuild;
using terrain::PlanetLevel;

void testCompact(const terrain::HeightfieldParams& relief) {
    const PlanetBuild build = golden::smallBuild(relief, 2);
    PlanetBuild copy = build;
    const PlanetLevel level(std::move(copy), terrain::PlanetParams{});
    const terrain::PlanetCells& cells = level.cells();
    bool quantized = true;
    bool flags = true;
    bool climate = true;
    for (std::size_t c = 0; c < build.grid.cells(); ++c) {
        quantized = quantized && std::abs(level.elevation(c) - build.height[c]) <= 0.5f &&
                    std::abs(level.precipitation(c) - build.climate.precipitation[c]) <= 0.5f;
        flags = flags && cells.receiver(c) == build.receiver[c] &&
                cells.ocean(c) == (build.ocean[c] != terrain::planet::kLand) &&
                cells.lakeAt(c) == build.lakeOfCell[c];
        const float t = terrain::planet::cellTemperature(
            build.climate.rowTemperature[static_cast<std::size_t>(build.grid.row(c))],
            build.height[c], terrain::PlanetClimateParams{});
        climate = climate && std::abs(level.temperature(c) - t) < 0.01f;
    }
    TEST_CHECK(quantized && flags && climate);
    // Every river cell of the build is found, with its discharge.
    std::size_t rivers = 0;
    bool discharge = true;
    for (std::size_t c = 0; c < build.grid.cells(); ++c) {
        if (const terrain::PlanetCells::RiverCell* river = cells.riverAt(c)) {
            ++rivers;
            const terrain::RiverSegment& s =
                level.rivers().segments[static_cast<std::size_t>(river->segment)];
            discharge =
                discharge && s.vertices[static_cast<std::size_t>(river->vertex)].cell == c &&
                s.vertices[static_cast<std::size_t>(river->vertex)].discharge == build.discharge[c];
        }
    }
    TEST_CHECK(rivers > 0 && rivers == cells.riverCells.size() && discharge);
    // The basins recomputed from the compact form are those of the build.
    const terrain::planet::LakeAt lakeAt = [&](std::size_t c) { return build.lakeOfCell[c]; };
    TEST_CHECK(level.basins() == terrain::planet::computeBasins(
                                     build.grid, build.ocean, build.receiver, lakeAt, build.lakes));
    // About 5 bytes per cell, plus the sparse lakes and rivers (the river cells from 10 m3/s on).
    const std::size_t sparse =
        cells.riverCells.size() * sizeof(terrain::PlanetCells::RiverCell) +
        cells.lakeCells.size() * sizeof(terrain::PlanetCells::LakeCell);
    TEST_CHECK(cells.memoryBytes() >= 5 * build.grid.cells());
    TEST_CHECK(cells.memoryBytes() < 6 * build.grid.cells() + sparse);
}

void testQuantization() {
    terrain::PlanetBuild build = golden::smallBuild(terrain::HeightfieldParams{}, 1);
    build.height[0] = 40000.0f;
    build.height[1] = -40000.0f;
    build.height[2] = 12.5f;
    build.climate.precipitation[0] = 70000.0f;
    build.climate.precipitation[1] = 0.4f;
    const terrain::PlanetCells cells = terrain::planet::compactCells(build);
    TEST_CHECK(cells.elevation[0] == 32767 && cells.elevation[1] == -32768 &&
               cells.elevation[2] == 13);
    TEST_CHECK(cells.precipitation[0] == 65535 && cells.precipitation[1] == 0);
}

} // namespace

int main() {
    testCompact(terrain::HeightfieldParams{});
    testCompact(golden::smallPlanetaryParams());
    testQuantization();
    return test::result();
}
