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
    // Recollé par « Tests: horizon 8-9 of hydrology_bits_test re-pasted after #84 » (agent 39) :
    // "horizon 8-9" ; avec #84 (agent 49, les biomes des terres), les texels de l'horizon portent
    // l'eau de leur colonne et la place d'un couvert (BlendTexel::biomes), et les biomes du monde
    // changent (21 biomes) ; kGeneratorVersion passe à 10 sur main. Même valeur sur main
    // (f6b95b7) et sur la branche : l'agent 39 ne change aucun bit. Avant : 0x0CB9CFA8700C153A,
    // recollé à la fusion de #76 (texels de 16 octets).
    expect("horizon 8-9", horizon.value(), 0xDB7DF1A9ACC2A16AULL, secondsSince(start));
    std::printf("courses computed: %zu, %zu bytes\n", h.coursesComputed(), h.courseBytes());
    return test::result();
}
