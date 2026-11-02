#pragma once

#include "terrain/generation/planet/planet_level.hpp"

#include <cstdint>
#include <filesystem>

namespace terrain {

// The planet level of a world on disk (phase 11B): computed once when the world is created (a
// few minutes for the whole planet), then read back in seconds. The file planet.bin of the folder
// of the world, framed like the other files of the world (magic, version, size, FNV-1a checksum),
// holds what the level keeps (PlanetLevel::Parts) and what it was built from: the seed, the
// version of the generator of the relief (kReliefGeneratorVersion), the world and every parameter of the level.
// It is written whole or not at all, and its level is checked against its checksum when read.

inline constexpr const char* kPlanetFileName = "planet.bin";
inline constexpr std::uint32_t kPlanetFileVersion = 1;

// Writes level, built from seed on world, to file. Throws StorageError (Io).
void savePlanetFile(const std::filesystem::path& file, const PlanetLevel& level,
                    std::uint64_t seed, const WorldSize& world);

// Reads back from file the level of seed on world with params. Throws StorageError: Io if the
// file cannot be read; Truncated, Corrupt or Invalid if it is damaged; NewerVersion; Invalid
// ("stale") if it holds the level of another seed, world, generator version or parameters (the
// caller builds the level again then).
[[nodiscard]] PlanetLevel loadPlanetFile(const std::filesystem::path& file, std::uint64_t seed,
                                         const PlanetParams& params, const WorldSize& world);

// The level of seed on world: read from file when it holds it, otherwise built from its relief
// (Heightfield(seed, heightfield, world)) and written to file. A file that cannot be written
// leaves the level built all the same. built, when not null, says whether it was built. probe,
// if set, hears of "load", or of the stages of the build, "level" and "save".
[[nodiscard]] PlanetLevel openPlanetLevel(const std::filesystem::path& file, std::uint64_t seed,
                                          const HeightfieldParams& heightfield,
                                          const PlanetParams& params, const WorldSize& world,
                                          bool* built = nullptr,
                                          const PlanetStageProbe& probe = {});

} // namespace terrain
