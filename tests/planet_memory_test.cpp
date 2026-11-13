// The planet level created in little memory (phase 11D), on the small world: the level of the
// lean build (planet::buildLevelParts) is the golden one and that of the full build, on 1 and 4
// threads, and the heap at its peak stays within a bound proportional to the grid. The heap is
// counted by the operator new and delete of heap_counter.cpp, linked into this test alone.
#include "terrain/generation/planet/planet_level.hpp"

#include "heap_counter.hpp"
#include "planet_golden_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using terrain::PlanetLevel;

// Bytes per cell of the small world the lean build may hold at its peak, above its relief field:
// its peak is the climate, which keeps about 4 r rows of the storm moisture in double (r its
// radius in cells, at most half the rows): on the small world nearly twice the grid, 16 bytes per
// cell. It measures 28.6 to 29.0 bytes per cell; the full build of the same level (buildPlanet,
// the arrays of phase 11B) 40 to 46.
constexpr double kBytesPerCell = 32.0;

// The checksum of the lean level, the peak of its build above the field and the stage of it.
struct Peak {
    std::uint64_t checksum = 0;
    double bytesPerCell = 0.0;
    std::string stage;
};

Peak leanLevel(const terrain::Heightfield& field, const terrain::PlanetParams& params,
               double cells) {
    Peak peak;
    const std::size_t before = heap_counter::live();
    heap_counter::resetPeak();
    const terrain::PlanetStageProbe probe = [&](const char* stage) {
        const double bytes = static_cast<double>(heap_counter::peak() - before) / cells;
        if (bytes > peak.bytesPerCell) {
            peak.bytesPerCell = bytes;
            peak.stage = stage;
        }
        heap_counter::resetPeak();
    };
    const PlanetLevel level(terrain::planet::buildLevelParts(field, params, probe));
    peak.checksum = level.checksum();
    return peak;
}

void testLevel(const char* name, const terrain::HeightfieldParams& relief, int samples,
               std::uint64_t golden) {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, relief, golden::kSmallPlanet);
    const terrain::PlanetGrid grid =
        terrain::makePlanetGrid(golden::kSmallPlanet, terrain::PlanetParams{}.cellSize);
    const auto cells = static_cast<double>(grid.cells());
    for (const int threads : {1, 4}) {
        const terrain::PlanetParams params = golden::smallPlanetParams(threads, samples);
        const Peak lean = leanLevel(field, params, cells);
        const std::size_t before = heap_counter::live();
        heap_counter::resetPeak();
        const PlanetLevel full(terrain::planet::buildPlanet(field, params), params);
        const double fullPeak = static_cast<double>(heap_counter::peak() - before) / cells;
        std::printf("%s, %d threads: heap peak %.1f bytes per cell (%s; full build %.1f), "
                    "checksum %016llx\n",
                    name, threads, lean.bytesPerCell, lean.stage.c_str(), fullPeak,
                    static_cast<unsigned long long>(lean.checksum));
        TEST_CHECK(lean.checksum == golden && full.checksum() == golden);
        TEST_CHECK(lean.bytesPerCell <= kBytesPerCell);
    }
}

} // namespace

int main() {
    // The golden levels of planet_golden_test: the default relief with 2 x 2 samples per cell,
    // the scaled planetary relief with one.
    testLevel("default relief", terrain::HeightfieldParams{}, 2, 0xCDBFA06D845540C8ULL);
    testLevel("planetary relief", golden::smallPlanetaryParams(), 1, 0x4B75A8B9F9E442BFULL);
    return test::result();
}
