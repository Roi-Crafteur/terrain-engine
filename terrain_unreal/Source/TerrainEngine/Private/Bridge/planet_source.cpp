#include "planet_source.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/storage/storage_error.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <utility>

namespace terrain::unreal {

namespace {

constexpr const char* kSeedToken = "{Seed}";

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

std::string resolvePlanetPath(const std::string& path, std::uint64_t seed,
                              const std::string& base) {
    if (path.empty()) {
        return {};
    }
    std::string named = path;
    const std::string token(kSeedToken);
    const std::string value = std::to_string(seed);
    for (std::size_t at = named.find(token); at != std::string::npos;
         at = named.find(token, at + value.size())) {
        named.replace(at, token.size(), value);
    }
    const std::filesystem::path file(reinterpret_cast<const char8_t*>(named.c_str()));
    if (file.is_absolute() || base.empty()) {
        return named;
    }
    const std::filesystem::path full =
        std::filesystem::path(reinterpret_cast<const char8_t*>(base.c_str())) / file;
    const std::u8string text = full.lexically_normal().generic_u8string();
    return {text.begin(), text.end()};
}

PlanetSource openPlanetSource(const std::filesystem::path& file, std::uint64_t seed,
                              const WorldSize& world, const PlanetStageProbe& probe) {
    const auto start = std::chrono::steady_clock::now();
    PlanetSource source;
    bool newer = false;
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        source.reason = "no file";
    } else {
        try {
            source.level = std::make_shared<const PlanetLevel>(
                loadPlanetFile(file, seed, PlanetParams{}, world));
            source.seconds = secondsSince(start);
            return source;
        } catch (const StorageError& e) {
            source.reason = e.what();
            newer = e.kind() == StorageErrorKind::NewerVersion;
        }
    }
    source.built = true;
    source.level = std::make_shared<const PlanetLevel>(seed, HeightfieldParams{}, PlanetParams{},
                                                       world, probe);
    if (newer) {
        source.writeError = "a file of a newer version of the game is never replaced";
    } else {
        std::filesystem::create_directories(file.parent_path(), error);
        try {
            savePlanetFile(file, *source.level, seed, world);
            source.written = true;
        } catch (const StorageError& e) {
            source.writeError = e.what();
        }
    }
    source.seconds = secondsSince(start);
    return source;
}

std::string describe(const PlanetSource& source, const std::string& file) {
    std::array<char, 64> seconds{};
    std::snprintf(seconds.data(), seconds.size(), "%.1f s", source.seconds);
    if (!source.built) {
        return "planet level read from " + file + " in " + seconds.data();
    }
    std::string line = "planet level BUILT (" + source.reason + ") in " + seconds.data();
    line += source.written ? ", written to " + file
                           : ", kept in memory only (" + source.writeError + ")";
    return line;
}

} // namespace terrain::unreal
