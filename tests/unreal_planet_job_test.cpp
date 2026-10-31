// The planet file of Terrain.Seed (Bridge/planet_job.hpp): which file a seed reads and where it
// may be built, a level built on a thread of its own and read back the next time, a file of the
// user never written (its bytes unchanged) whether it holds the seed or another one, and a
// cancelled build that leaves no file.
#include "planet_job.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include "terrain/storage/planet_file.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::unreal::choosePlanetFile;
using terrain::unreal::PlanetChoice;
using terrain::unreal::PlanetJob;

constexpr std::uint64_t kSeedA = 20261019;
constexpr std::uint64_t kSeedB = 7;

std::string text(const std::filesystem::path& path) {
    const std::u8string u8 = path.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::vector<char> bytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void testChoice() {
    TEST_CHECK(choosePlanetFile("", kSeedB, "/p") == PlanetChoice{});
    const PlanetChoice owned = choosePlanetFile("Saved/TerrainEngine/planet_{Seed}.bin", 7, "/p");
    TEST_CHECK(owned.file == "/p/Saved/TerrainEngine/planet_7.bin" && owned.mayWrite);
    TEST_CHECK(owned.fallback.empty());
    const PlanetChoice user = choosePlanetFile("/data/my_planet.bin", 7, "/p");
    TEST_CHECK(user.file == "/data/my_planet.bin" && !user.mayWrite);
    TEST_CHECK(user.fallback == "/p/Saved/TerrainEngine/dev/planet_7.bin");
    const PlanetChoice relative = choosePlanetFile("Planets/mine.bin", 7, "/p");
    TEST_CHECK(relative.file == "/p/Planets/mine.bin" && !relative.mayWrite);
    // A file of the user named as the fallback would be: the fallback goes aside.
    const PlanetChoice same = choosePlanetFile("Saved/TerrainEngine/dev/planet_7.bin", 7, "/p");
    TEST_CHECK(same.file == "/p/Saved/TerrainEngine/dev/planet_7.bin" && !same.mayWrite);
    TEST_CHECK(same.fallback == "/p/Saved/TerrainEngine/dev/planet_7_built.bin");
}

terrain::unreal::PlanetJobResult runJob(const PlanetChoice& choice, std::uint64_t seed) {
    PlanetJob job(choice, seed, golden::kSmallPlanet);
    while (!job.done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    TEST_CHECK(job.stage() == "done" && job.seconds() > 0.0);
    return job.take();
}

void testJobs(const std::filesystem::path& dir) {
    const std::string base = text(dir);
    // Built on its thread and written to the file of its seed, then read back from it.
    const PlanetChoice owned = choosePlanetFile("planet_{Seed}.bin", kSeedA, base);
    auto built = runJob(owned, kSeedA);
    TEST_CHECK(built.error.empty() && !built.cancelled && built.file == owned.file);
    TEST_CHECK(built.source.level != nullptr && built.source.built && built.source.written);
    TEST_CHECK(built.source.reason == "no file");
    auto read = runJob(owned, kSeedA);
    TEST_CHECK(read.source.level != nullptr && !read.source.built && read.file == owned.file);
    TEST_CHECK(read.source.level->grid().nx == built.source.level->grid().nx);
    // A file of the user holding seed A: read for seed A, untouched for seed B, which is built in
    // the folder of the tools.
    const std::filesystem::path userFile = dir / "user_planet.bin";
    std::filesystem::copy_file(owned.file, userFile);
    const std::vector<char> before = bytes(userFile);
    const PlanetChoice user = choosePlanetFile(text(userFile), kSeedA, base);
    auto same = runJob(user, kSeedA);
    TEST_CHECK(same.file == text(userFile) && !same.source.built);
    const PlanetChoice other = choosePlanetFile(text(userFile), kSeedB, base);
    auto aside = runJob(other, kSeedB);
    std::printf("  seed B: %s\n", terrain::unreal::describe(aside.source, aside.file).c_str());
    TEST_CHECK(aside.file == other.fallback && aside.source.built && aside.source.written);
    TEST_CHECK(aside.source.reason.find("user_planet.bin") != std::string::npos);
    TEST_CHECK(bytes(userFile) == before);
    TEST_CHECK(std::filesystem::exists(other.fallback));
    // A missing file of the user is never created either.
    const std::filesystem::path missing = dir / "missing.bin";
    auto none = runJob(choosePlanetFile(text(missing), kSeedB, base), kSeedB);
    TEST_CHECK(!std::filesystem::exists(missing) && none.source.level != nullptr);
}

void testCancel(const std::filesystem::path& dir) {
    const PlanetChoice choice = choosePlanetFile("cancelled_{Seed}.bin", 99, text(dir));
    PlanetJob job(choice, 99, golden::kSmallPlanet);
    job.cancel();
    job.wait();
    const auto result = job.take();
    TEST_CHECK(job.done() && result.cancelled && result.source.level == nullptr);
    TEST_CHECK(!std::filesystem::exists(choice.file));
    // Destroyed while it builds: it stops at the end of its stage, or ends its build if the
    // small world is built first (a fast machine); its file is then whole, never half written.
    const auto start = std::chrono::steady_clock::now();
    {
        PlanetJob dropped(choosePlanetFile("dropped_{Seed}.bin", 98, text(dir)), 98,
                          golden::kSmallPlanet);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const std::filesystem::path dropped = dir / "dropped_98.bin";
    const bool written = std::filesystem::exists(dropped);
    std::printf("  dropped while building: %s in %.2f s\n", written ? "built" : "stopped",
                seconds);
    TEST_CHECK(seconds < 10.0);
    if (written) {
        const terrain::PlanetLevel level =
            terrain::loadPlanetFile(dropped, 98, terrain::PlanetParams{}, golden::kSmallPlanet);
        TEST_CHECK(level.grid().nx > 0);
    }
    TEST_CHECK(!std::filesystem::exists(dir / "dropped_98.bin.tmp"));
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "unreal_planet_job_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    testChoice();
    testJobs(dir);
    testCancel(dir);
    std::filesystem::remove_all(dir);
    std::printf("unreal_planet_job_test: %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return test::result();
}
