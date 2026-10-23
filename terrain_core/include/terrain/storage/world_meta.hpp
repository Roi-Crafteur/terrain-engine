#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace terrain {

// Version of the world format (world.meta and the region files) written by this build. A world
// of a newer format is refused; an older one is read, and written in the current format when
// saved.
// 1: phase 9, the blocks of the region files one by one.
// 2: phase 15, the blocks of the region files by sections (chunk_record.hpp); world.meta is
//    unchanged, its version tells the builds of format 1 that they cannot read the regions.
inline constexpr std::uint32_t kWorldFormatVersion = 2;
// First format whose region files hold sections.
inline constexpr std::uint32_t kSectionFormatVersion = 2;

// Version of the terrain generator: the same seed and parameters give the same terrain only with
// the same generator version. Every change of the generated values (relief, caves, biomes,
// plants...) must increment it. Kept here for now; it belongs to generation/.
// 2: phase 9 (deterministic math, wrapped world).
// 3: phase 11R (the planetary geography: continents, plate belts, provinces, drained valleys).
// 4: phase 11B (the sea decided by the geography, depressions below the sea level, the water:
// rivers carved, lakes, falls).
inline constexpr std::uint32_t kGeneratorVersion = 4;

// Generator of the worlds saved before the world format (TMOD files): the one up to phase 8C-2.
inline constexpr std::uint32_t kLegacyGeneratorVersion = 1;

// The world sizes are multiples of this step, in meters: chunks (16 m), regions (512 m), region
// groups (16,384 m) and the future far tiles all fit exactly at the seams of the wrapped world.
inline constexpr std::uint64_t kWorldSizeStep = 65'536;
// Largest world side, in meters: the same bound as the generator (WorldSize, kMaxWorldMeters).
inline constexpr std::uint64_t kMaxWorldSize = std::uint64_t{1} << 30;
// The default world, about 60,000 x 35,000 km.
inline constexpr std::uint64_t kDefaultWorldSizeX = 916 * kWorldSizeStep; // 60,030,976 m
inline constexpr std::uint64_t kDefaultWorldSizeZ = 534 * kWorldSizeStep; // 34,996,224 m

inline constexpr std::size_t kMaxWorldNameBytes = 128;
inline constexpr std::size_t kMaxSettingKeyBytes = 64;
// Tags of world.meta with this bit are optional fields: a build that does not know one keeps it
// without reading it.
inline constexpr std::uint16_t kOptionalFieldBit = 0x8000;

struct SpawnPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const SpawnPoint&, const SpawnPoint&) = default;
};

// Metadata of a world, its world.meta file. The world is centered on the origin:
// x in [-sizeX / 2, sizeX / 2) west to east, z in [-sizeZ / 2, sizeZ / 2), the equator at z = 0.
// The storage never reads the clock: the dates are set by the caller (unixNow).
struct WorldMeta {
    std::string name; // shown to the player, UTF-8
    std::uint64_t seed = 0;
    std::uint64_t sizeX = kDefaultWorldSizeX; // meters
    std::uint64_t sizeZ = kDefaultWorldSizeZ; // meters
    std::uint32_t generatorVersion = kGeneratorVersion;
    // Generation parameters by name; empty for the defaults of the generator version. Stored
    // only: the generator does not read them yet.
    std::map<std::string, double> generation;
    std::int64_t createdAt = 0; // Unix time, seconds (UTC)
    std::int64_t savedAt = 0;   // Unix time of the last save, seconds (UTC)
    SpawnPoint spawn;           // meters
    std::uint64_t playTimeSeconds = 0;
    // Optional fields this build does not know, by tag (kOptionalFieldBit set): written back as
    // they were read, so that a newer build loses nothing.
    std::map<std::uint16_t, std::vector<std::uint8_t>> unknownOptionalFields;

    friend bool operator==(const WorldMeta&, const WorldMeta&) = default;
};

// A world name: valid UTF-8 of 1 to kMaxWorldNameBytes bytes, without control characters.
[[nodiscard]] bool isValidWorldName(std::string_view name);

// Throws StorageError (Invalid) naming the first invalid value: the name, a size that is not a
// positive multiple of kWorldSizeStep up to kMaxWorldSize, a parameter name outside
// [a-z0-9_.]{1,64} or a non-finite value, a spawn point outside the world, an unknown field
// without kOptionalFieldBit.
void validateWorldMeta(const WorldMeta& meta);

// world.meta, little-endian:
//   frame: "TWLD", u32 format version, u64 size of the whole file, fields, u32 FNV-1a of every
//   byte before it;
//   fields: u16 tag, u32 size, payload, by increasing tag; all of them required but those with
//   kOptionalFieldBit:
//     1 name: UTF-8 bytes          2 seed: u64             3 size: u64 x, u64 z (meters)
//     4 generator version: u32     5 generation: u32 count, per parameter by increasing name:
//                                    u8 length, name, f64 value
//     6 created: i64               7 saved: i64            8 spawn: f64 x, f64 y, f64 z
//     9 play time: u64 seconds.
// The same metadata always give the same bytes. Throws StorageError (Invalid) for an invalid
// meta (validateWorldMeta).
[[nodiscard]] std::vector<std::uint8_t> serializeWorldMeta(const WorldMeta& meta);

// Throws StorageError unless bytes are a whole world.meta of a format this build reads: see
// storage_error.hpp for the kinds. An unknown required field is Invalid, an unknown optional
// one is kept.
[[nodiscard]] WorldMeta deserializeWorldMeta(std::span<const std::uint8_t> bytes);

// The world was generated by another generator version: the terrain away from the modifications
// may differ from the one its players saw.
[[nodiscard]] bool generatorChanged(const WorldMeta& meta);
// Warning for the player if generatorChanged, empty otherwise.
[[nodiscard]] std::string generatorWarning(const WorldMeta& meta);

// Current Unix time, in seconds: for the caller to fill createdAt and savedAt.
[[nodiscard]] std::int64_t unixNow();

} // namespace terrain
