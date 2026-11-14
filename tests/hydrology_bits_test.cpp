// The bits of a sample of the hydrology of the small world (agent 39), pinned before the work on
// its speed and its stops: the courses of reaches, the queries at points on the lattices of the
// coarse levels of the horizon and near a fall, and pieces of the levels 8 and 9 of the horizon
// over the water. Faster courses, stoppable computations and caches without locks change none of
// them (hydrology_sample_support.hpp).
#include "hydrology_sample_support.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>

namespace {

using hydrology_sample::Checksum;

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void expect(const char* part, std::uint64_t got, std::uint64_t expected, double seconds) {
    std::printf("%s: 0x%016" PRIX64 " (%.2f s)\n", part, got, seconds);
    if (got != expected) {
        std::fprintf(stderr, "%s: 0x%016" PRIX64 ", expected 0x%016" PRIX64 "\n", part, got,
                     expected);
    }
    TEST_CHECK(got == expected);
}

} // namespace

int main() {
    const map_world::WetWorld world;
    const terrain::Hydrology& h = *world.hydrology;

    auto start = std::chrono::steady_clock::now();
    Checksum reaches;
    for (const std::size_t r : hydrology_sample::sampleReaches(h)) {
        hydrology_sample::addReach(reaches, h.reach(r));
    }
    expect("reaches", reaches.value(), 0xF1684975E5044E29ULL, secondsSince(start));

    start = std::chrono::steady_clock::now();
    Checksum points;
    for (const auto& [x, z] : hydrology_sample::samplePoints(*world.planet)) {
        hydrology_sample::addPoint(points, h, x, z);
    }
    expect("points", points.value(), 0x6E93AB16CCEB1DAFULL, secondsSince(start));

    start = std::chrono::steady_clock::now();
    const terrain::HorizonSampler sampler(world.tiles.heightfield(), world.tiles.ecology(),
                                          hydrology_sample::horizonParams());
    Checksum horizon;
    for (terrain::HorizonPiece& piece : hydrology_sample::samplePieces(*world.planet)) {
        terrain::computePiece(sampler, piece);
        horizon.add(piece.level);
        for (const terrain::HorizonCell& cell : piece.cells) {
            hydrology_sample::addCell(horizon, cell);
        }
    }
    // Recollé par « Merge main (#75 parallel CI, #76 biome format, #79 guards) into the hydrology
    // branch of agent 39 » : "horizon 8-9" ; les texels de biomes de #76 (agent 49) ont 16 octets
    // (quatre champs sommés au lieu de ids et weights) et lisent l'eau du core. Même valeur sur
    // main (38b5a82) et sur la branche : l'agent 39 ne change aucun bit.
    expect("horizon 8-9", horizon.value(), 0x0CB9CFA8700C153AULL, secondsSince(start));
    std::printf("courses computed: %zu, %zu bytes\n", h.coursesComputed(), h.courseBytes());
    return test::result();
}
