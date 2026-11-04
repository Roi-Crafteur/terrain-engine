// The planet file of the tests of the places of the coasts (coast_planet_support.hpp): the default
// world with its planet level as it was before the hydrographic network of task 11C
// (golden::beforeNetwork), read when the file already holds it, else built and written there.
// Pinned (agent 31, task 11C step 1): the rivers of the network move, and the places of the coasts
// the tests stand on (the needle stack, its reefs, the inlets) are cut by the corridors of the
// rivers; step 2 of task 11C moves them onto the default planet level.
#include "terrain/storage/planet_file.hpp"

#include "planet_golden_support.hpp"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: coast_planet_build <file>\n");
        return 1;
    }
    try {
        bool built = false;
        const terrain::PlanetLevel level = terrain::openPlanetLevel(
            argv[1], terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
            golden::beforeNetwork(terrain::PlanetParams{}), terrain::WorldSize{}, &built);
        std::printf("%s: the planet level before the network, %s, checksum %016llx\n", argv[1],
                    built ? "built" : "read",
                    static_cast<unsigned long long>(level.checksum()));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "coast_planet_build: %s\n", e.what());
        return 1;
    }
    return 0;
}
