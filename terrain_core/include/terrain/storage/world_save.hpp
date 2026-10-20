#pragma once

#include "terrain/storage/modifications.hpp"
#include "terrain/storage/region_file.hpp"
#include "terrain/storage/world_meta.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace terrain {

// A world is a folder: every client regenerates the terrain from the seed, so the folder only
// holds the metadata and the modifications of the players:
//   world.meta                         the metadata (world_meta.hpp)
//   regions/<gx>.<gz>/<x>.<z>.treg     the modifications, one file per region (region_file.hpp)
// Each saved file keeps its previous version beside it, <file>.bak, read if the file is damaged
// or missing; <file>.tmp files are never read. Other names are ignored.
inline constexpr const char* kWorldMetaFileName = "world.meta";

// Directories reserved for later versions of the format, never created, read or changed by this
// one: the players (positions, inventories), the gameplay data and the precomputed planet level
// (a cache, keyed by the seed, the generator version and the generation parameters).
inline constexpr std::array<const char*, 3> kReservedDirectoryNames = {"players", "gameplay",
                                                                      "planet"};

struct LoadedWorld {
    WorldMeta meta;
    ModificationStore modifications;
    bool generatorChanged = false;               // see generatorChanged (world_meta.hpp)
    std::vector<std::filesystem::path> restored; // files damaged or missing, read from their .bak
    std::vector<std::string> warnings;           // one message per issue above, for the player
};

// Saves a whole world: every region of modifications, then world.meta; removes the region files
// of the regions without modifications and the abandoned temporary files. Throws StorageError:
// Invalid for an invalid meta, before any write. Neither a failed save nor a crash or a power
// cut damages a file (see the limits below).
//
// Each file is replaced atomically and durably: written to <file>.tmp and flushed, the old file
// renamed <file>.bak, <file>.tmp renamed <file>, the directories flushed once. A save of several
// files is not one snapshot: after a crash, some regions may be newer than the others, each one
// whole. One process at a time may save a world.
void saveWorld(const std::filesystem::path& world, const WorldMeta& meta,
               const ModificationStore& modifications);

// Saves world.meta and the given regions only: the periodic save, the regions edited since the
// last save (regionOf the chunks the edits return) being known. A region without modifications
// loses its file.
void saveWorldRegions(const std::filesystem::path& world, const WorldMeta& meta,
                      const ModificationStore& modifications, std::span<const RegionCoord> regions);

// Saves world.meta alone: a rename, the play time.
void saveWorldMeta(const std::filesystem::path& world, const WorldMeta& meta);

// The metadata of a world, read from world.meta.bak if world.meta is damaged or missing
// (restored). Throws StorageError.
struct LoadedMeta {
    WorldMeta meta;
    bool restored = false;
};
[[nodiscard]] LoadedMeta loadWorldMeta(const std::filesystem::path& world);

// Loads a whole world into memory. Every file is checked: a damaged or missing file is read
// from its .bak copy and reported (restored, warnings); a file that can be read neither way, or
// is newer than this build, fails the load. Throws StorageError.
[[nodiscard]] LoadedWorld loadWorld(const std::filesystem::path& world);

// Loads one region (for the streaming, later): no modifications if it has no file. Atomic: the
// region is whole, or a StorageError is thrown.
struct LoadedRegion {
    ModificationStore modifications;
    bool restored = false;
};
[[nodiscard]] LoadedRegion loadRegion(const std::filesystem::path& world, RegionCoord region,
                                      std::uint64_t seed);

// Regions with a file or a .bak copy in a world, sorted. Throws StorageError: Io if the
// directories cannot be read, Invalid for a region file in the directory of another group.
[[nodiscard]] std::vector<RegionCoord> savedRegions(const std::filesystem::path& world);

} // namespace terrain
