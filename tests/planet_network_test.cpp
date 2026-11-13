// The network of the planet level (planet_network.hpp, task 11C step 1) on the small world: the
// discharge of every land cell kept to 0.05 %, kept by the file, orders that never fall downstream,
// areas that grow, basins that end where the water ends, watercourses of one kind, both banks.
#include "terrain/generation/planet/planet_network.hpp"
#include "terrain/storage/planet_file.hpp"

#include "network_flags_support.hpp"
#include "planet_golden_support.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>

namespace {

using terrain::PlanetNetwork;
using terrain::StreamBank;
using terrain::StreamEnd;
using terrain::StreamKind;

void testCodes() {
    double worst = 0.0;
    for (double q = 2.0e-6; q < 1.0e6; q *= 1.37) {
        const double back = terrain::planet::dischargeOf(terrain::planet::dischargeCode(q));
        worst = std::max(worst, std::abs(back - q) / q);
    }
    TEST_CHECK(worst < 5.0e-4);
    TEST_CHECK(terrain::planet::dischargeCode(0.0) == 0);
    TEST_CHECK(terrain::planet::dischargeOf(0) == 0.0);
}

void testSmallWorld() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                     golden::kSmallPlanet);
    const terrain::PlanetParams params =
        network_flags::flagged(golden::smallPlanetParams(2, 2), network_flags::kStepOne);
    terrain::PlanetBuild build = terrain::planet::buildPlanet(field, params);
    const std::vector<float> discharge = build.discharge;
    const terrain::PlanetLevel level(std::move(build), params);
    const PlanetNetwork net(level);
    const terrain::PlanetGrid& grid = level.grid();
    std::size_t land = 0;
    std::size_t falls = 0;
    std::size_t shrinks = 0;
    std::size_t basins = 0;
    std::size_t kinds = 0;
    std::size_t sides[3] = {0, 0, 0};
    double worst = 0.0;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (!net.land(c)) {
            continue;
        }
        ++land;
        if (discharge[c] > 1.0e-5f) {
            worst = std::max(worst, std::abs(net.discharge(c) - discharge[c]) / discharge[c]);
        }
        const std::int64_t d = net.down(c);
        if (d < 0) {
            basins += net.basin(c) == c ? 0 : 1;
            continue;
        }
        const auto down = static_cast<std::size_t>(d);
        falls += net.order(down) < net.order(c) ? 1 : 0;
        shrinks += net.area(down) <= net.area(c) && net.area(down) < 65535 ? 1 : 0;
        basins += net.basin(c) == net.basin(down) ? 0 : 1;
        if (net.mainStem(c)) {
            kinds += net.kind(c) == net.kind(down) ? 0 : 1;
        } else if (net.end(c) == StreamEnd::Confluence) {
            ++sides[static_cast<std::size_t>(net.bank(c))];
        }
    }
    std::printf("small world: %zu land cells, discharge kept to %.5f %%, %zu tributaries on the "
                "right bank, %zu on the left, %.1f MB\n",
                land, 100.0 * worst, sides[1], sides[2],
                static_cast<double>(net.memoryBytes()) * 1e-6);
    TEST_CHECK(worst < 5.0e-4);
    TEST_CHECK(falls == 0);
    TEST_CHECK(shrinks == 0);
    TEST_CHECK(basins == 0);
    TEST_CHECK(kinds == 0);
    TEST_CHECK(sides[0] == 0 && sides[1] > land / 50 && sides[2] > land / 50);
    // The kinds follow the size and the end: a fleuve reaches the sea.
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (net.land(c) && net.kind(c) == StreamKind::Fleuve) {
            const std::uint32_t b = net.basin(c);
            TEST_CHECK(net.end(b) == StreamEnd::Sea);
        }
    }
    // The file keeps the discharge of every land cell.
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "planet_network_test.bin";
    terrain::savePlanetFile(file, level, terrain::kDefaultWorldSeed, golden::kSmallPlanet);
    const terrain::PlanetLevel read =
        terrain::loadPlanetFile(file, terrain::kDefaultWorldSeed, params, golden::kSmallPlanet);
    std::filesystem::remove(file);
    TEST_CHECK(read.cells().landDischarge == level.cells().landDischarge);
    TEST_CHECK(read.checksum() == level.checksum());
}

} // namespace

int main() {
    testCodes();
    testSmallWorld();
    return test::result();
}
