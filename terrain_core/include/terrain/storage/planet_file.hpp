#pragma once

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/storage/storage_error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace terrain {

// The planet level of a world on disk (phase 11B): computed once when the world is created (a
// few minutes for the whole planet), then read back in seconds. The file planet.bin of the folder
// of the world, framed like the other files of the world (magic, version, size, FNV-1a checksum),
// holds what the level keeps (PlanetLevel::Parts) and what it was built from: the seed, the
// version of the generator of the relief (kReliefGeneratorVersion), the world and every parameter of the level.
// It is written whole or not at all, and its level is checked against its checksum when read.
//
// Version 2 (agent 51) adds the experimental flags the level was built with (experimental_flags.hpp,
// their canonical text) before the level, and its sections after it (what an agent adds to the
// level: src/storage/planet_sections.hpp says how). With neither, the file is of version 1, the
// bytes of main. A file of other flags is never read as the level asked, and never replaced
// (PlanetFlagsError).

inline constexpr const char* kPlanetFileName = "planet.bin";
inline constexpr std::uint32_t kPlanetFileVersion = 2;

// A planet file built with other experimental flags than those asked: refused, never built
// again over it (the caller tells which flags, and stops or reads another file).
class PlanetFlagsError : public StorageError {
public:
    PlanetFlagsError(const std::string& message, std::string stored, std::string wanted)
        : StorageError(StorageErrorKind::Invalid, message), stored_(std::move(stored)),
          wanted_(std::move(wanted)) {}

    [[nodiscard]] const std::string& stored() const { return stored_; } // those of the file
    [[nodiscard]] const std::string& wanted() const { return wanted_; } // those asked

private:
    std::string stored_;
    std::string wanted_;
};

// Writes level, built from seed on world with the experimental flags flags (their canonical
// text), to file. Throws StorageError (Io).
void savePlanetFile(const std::filesystem::path& file, const PlanetLevel& level,
                    std::uint64_t seed, const WorldSize& world, const std::string& flags = {});

// Reads back from file the level of seed on world with params and the experimental flags flags
// (canonical). Throws StorageError: Io if the file cannot be read; Truncated, Corrupt or Invalid
// if it is damaged; NewerVersion; PlanetFlagsError if it holds a level of other flags; Invalid
// ("stale") if it holds the level of another seed, world, generator version or parameters (the
// caller builds the level again then).
[[nodiscard]] PlanetLevel loadPlanetFile(const std::filesystem::path& file, std::uint64_t seed,
                                         const PlanetParams& params, const WorldSize& world,
                                         const std::string& flags = {});

// The level of seed on world: read from file when it holds it, otherwise built from its relief
// (Heightfield(seed, heightfield, world)) and written to file. A file that cannot be written
// leaves the level built all the same. built, when not null, says whether it was built. probe,
// if set, hears of "load", or of the stages of the build, "level" and "save". flags: the
// canonical text of the experimental flags heightfield and params were set with; a file of other
// flags throws PlanetFlagsError, nothing built.
[[nodiscard]] PlanetLevel openPlanetLevel(const std::filesystem::path& file, std::uint64_t seed,
                                          const HeightfieldParams& heightfield,
                                          const PlanetParams& params, const WorldSize& world,
                                          bool* built = nullptr,
                                          const PlanetStageProbe& probe = {},
                                          const std::string& flags = {});

} // namespace terrain
