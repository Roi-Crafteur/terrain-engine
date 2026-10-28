// Writes the planet level of a world to its file (phase 11B): what the game reads when it opens
// the world (the Unreal Bridge: ATerrainWorld::PlanetFile). Builds the level from the relief (a few
// minutes for the whole planet), unless the file already holds it; then reads it back to check it.
// With --memory, prints for each stage its seconds, the heap at its end and at its peak, and the
// peak of the process (MiB), then the peaks of the whole run.
// Usage: terrain_make_planet <file> [--seed <seed>] [--world <x> <z>] [--threads <n>] [--memory]

#include "heap_counter.hpp"
#include "process_memory.hpp"
#include "terrain/storage/planet_file.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
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
    bool memory = false;
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
        } else if (flag == "--memory") {
            ok = o.memory = true;
        }
        if (!ok) {
            std::fprintf(stderr, "Invalid option '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

double mebibytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// The time and memory of each stage (--memory).
class StageMeter {
public:
    StageMeter() {
        std::printf("%-10s %8s %10s %10s %13s  (MiB)\n", "stage", "seconds", "heap end",
                    "heap peak", "process peak");
        heap_counter::resetPeak();
    }

    void operator()(const char* stage) {
        const auto now = std::chrono::steady_clock::now();
        const std::size_t heapPeak = heap_counter::peak();
        const std::size_t processPeak = sampler_.takePeak();
        std::printf("%-10s %8.1f %10.0f %10.0f %13.0f\n", stage,
                    std::chrono::duration<double>(now - start_).count(),
                    mebibytes(heap_counter::live()), mebibytes(heapPeak), mebibytes(processPeak));
        std::fflush(stdout);
        heapMost_ = heapPeak > heapMost_ ? heapPeak : heapMost_;
        heap_counter::resetPeak();
        start_ = std::chrono::steady_clock::now();
    }

    void summary() const {
        std::printf("peaks: heap %.0f MiB, process %.0f MiB\n", mebibytes(heapMost_),
                    mebibytes(process_memory::peakResident()));
    }

private:
    process_memory::Sampler sampler_;
    std::size_t heapMost_ = 0;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
                     "Usage: %s <file> [--seed <seed>] [--world <x> <z>] [--threads <n>] "
                     "[--memory]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }
    try {
        terrain::validate(o.world);
        terrain::PlanetParams params;
        params.threads = o.threads;
        const std::unique_ptr<StageMeter> meter =
            o.memory ? std::make_unique<StageMeter>() : nullptr;
        terrain::PlanetStageProbe probe;
        if (meter) {
            probe = [&meter](const char* stage) { (*meter)(stage); };
        }
        const auto start = std::chrono::steady_clock::now();
        bool built = false;
        std::uint64_t checksum = 0;
        {
            // Only its checksum is kept: the file is read back without the level in memory.
            const terrain::PlanetLevel level = terrain::openPlanetLevel(
                o.file, o.seed, terrain::HeightfieldParams{}, params, o.world, &built, probe);
            checksum = level.checksum();
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        // Read back: the file holds this very level.
        const std::uint64_t again =
            terrain::loadPlanetFile(o.file, o.seed, params, o.world).checksum();
        if (meter) {
            (*meter)("read back");
            meter->summary();
        }
        if (again != checksum) {
            std::fprintf(stderr, "%s does not read back\n", o.file.string().c_str());
            return EXIT_FAILURE;
        }
        std::error_code error;
        const auto bytes = std::filesystem::file_size(o.file, error);
        std::printf("%s: the planet level of seed %llu, %s in %.1f s, %.0f MB, checksum %016llx\n",
                    o.file.string().c_str(), static_cast<unsigned long long>(o.seed),
                    built ? "built and written" : "already there", seconds,
                    error ? 0.0 : static_cast<double>(bytes) / 1.0e6,
                    static_cast<unsigned long long>(checksum));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
