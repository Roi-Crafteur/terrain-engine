// Writes the planet level of a world to its file (phase 11B): what the game reads when it opens
// the world (the Unreal Bridge: ATerrainWorld::PlanetFile). Builds the level from the relief (a few
// minutes for the whole planet), unless the file already holds it; then reads it back to check it.
// Usage: terrain_make_planet <file> [--seed <seed>] [--world <x> <z>] [--threads <n>]

#include "terrain/storage/planet_file.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

struct Options {
    std::filesystem::path file;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    int threads = 0;
};

bool parseOptions(int argc, char** argv, Options& o) {
    if (argc < 2 || argv[1][0] == '-') {
        return false;
    }
    o.file = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        bool ok = false;
        if (flag == "--seed" && i + 1 < argc) {
            ok = parse(argv[++i], o.seed);
        } else if (flag == "--world" && i + 2 < argc) {
            ok = parse(argv[i + 1], o.world.x) && parse(argv[i + 2], o.world.z);
            i += 2;
        } else if (flag == "--threads" && i + 1 < argc) {
            ok = parse(argv[++i], o.threads) && o.threads >= 0;
        }
        if (!ok) {
            std::fprintf(stderr, "Invalid option '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <file> [--seed <seed>] [--world <x> <z>] [--threads <n>]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    try {
        terrain::validate(o.world);
        terrain::PlanetParams params;
        params.threads = o.threads;
        const auto start = std::chrono::steady_clock::now();
        bool built = false;
        const terrain::PlanetLevel level = terrain::openPlanetLevel(
            o.file, o.seed, terrain::HeightfieldParams{}, params, o.world, &built);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        // Read back: the file holds this very level.
        const terrain::PlanetLevel again =
            terrain::loadPlanetFile(o.file, o.seed, params, o.world);
        if (again.checksum() != level.checksum()) {
            std::fprintf(stderr, "%s does not read back\n", o.file.string().c_str());
            return EXIT_FAILURE;
        }
        std::error_code error;
        const auto bytes = std::filesystem::file_size(o.file, error);
        std::printf("%s: the planet level of seed %llu, %s in %.1f s, %.0f MB, checksum %016llx\n",
                    o.file.string().c_str(), static_cast<unsigned long long>(o.seed),
                    built ? "built and written" : "already there", seconds,
                    error ? 0.0 : static_cast<double>(bytes) / 1.0e6,
                    static_cast<unsigned long long>(level.checksum()));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
