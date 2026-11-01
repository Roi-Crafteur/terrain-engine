#pragma once

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace terrain::unreal {

// The planet file of an Unreal level (agent 27): path, UTF-8, with every "{Seed}" replaced by the
// decimal seed; a relative path is taken from base (the folder of the game project), an absolute
// one as it is; empty stays empty (no planet level: the relief alone).
[[nodiscard]] std::string resolvePlanetPath(const std::string& path, std::uint64_t seed,
                                            const std::string& base);

// How a session got its planet level: read back from its file, or built and why — never a level
// of another seed, world, version of the generator or parameters read in silence.
struct PlanetSource {
    std::shared_ptr<const PlanetLevel> level;
    bool built = false;
    // Why it was built: there was no file, or what loadPlanetFile said of it (stale: another seed,
    // world, generator version or parameters; truncated or unlike its checksum; unreadable).
    std::string reason;
    bool written = false; // built and written to the file (false: kept in memory only)
    std::string writeError;
    double seconds = 0.0; // read or built (and written)
};

// The planet level of seed on world (HeightfieldParams{}, PlanetParams{}) in file: read when the
// file holds it, otherwise built and written there (its folder created), as openPlanetLevel, the
// reason told. A file of a newer version of the game is never replaced. probe, if set, hears of
// each stage of a build (PlanetLevel), and may throw to stop it there. Throws what the build
// throws.
[[nodiscard]] PlanetSource openPlanetSource(const std::filesystem::path& file, std::uint64_t seed,
                                            const WorldSize& world,
                                            const PlanetStageProbe& probe = {});

// One line for the log: what was read or built, why, in how long.
[[nodiscard]] std::string describe(const PlanetSource& source, const std::string& file);

} // namespace terrain::unreal
