#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/storage/modifications.hpp"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace terrain {

// A region: 32 x 32 chunks (512 m), whose modifications share a file. The region files of 32 x 32
// regions (16,384 m) share a directory, a region group, so that no directory holds millions of
// files. Shifts are exact floor divisions in C++20, negative coordinates included.
inline constexpr int kRegionShift = 5;
inline constexpr int kRegionChunks = 1 << kRegionShift;
inline constexpr int kRegionGroupShift = 5;
// Most chunks a region file holds.
inline constexpr std::size_t kMaxRegionChunks = std::size_t{1} << (2 * kRegionShift);
// Directory of the region groups, inside a world folder.
inline constexpr const char* kRegionDirectoryName = "regions";

struct RegionCoord {
    std::int32_t x = 0;
    std::int32_t z = 0;

    friend auto operator<=>(const RegionCoord&, const RegionCoord&) = default;
};

[[nodiscard]] RegionCoord regionOf(ChunkCoord chunk);
[[nodiscard]] RegionCoord regionGroupOf(RegionCoord region);

// Path of the file of a region inside a world folder: regions/<gx>.<gz>/<x>.<z>.treg, with the
// coordinates of its group then its own, in decimal ("regions/-1.0/-3.12.treg").
[[nodiscard]] std::filesystem::path regionRelativePath(RegionCoord region);

// "<x>.<z>" (a group directory) or "<x>.<z>.treg" (a region file) in canonical decimal: an
// optional '-', no leading zero, no "-0". nullopt for any other name.
[[nodiscard]] std::optional<RegionCoord> parseRegionGroupName(std::string_view name);
[[nodiscard]] std::optional<RegionCoord> parseRegionFileName(std::string_view name);

// Regions holding modifications in store, sorted.
[[nodiscard]] std::vector<RegionCoord> regionsOf(const ModificationStore& store);

// Region file, little-endian:
//   frame: "TREG", u32 format version (kWorldFormatVersion), u64 size of the whole file, body,
//   u32 FNV-1a of every byte before it;
//   body: u64 world seed, i32 region x, i32 region z, u32 chunk count (1 to 1024), then the
//   chunks of the region by z, then x, as in the modification file (modifications.hpp).
// Empty if store holds no modification in the region: a region without modifications has no
// file. The same modifications always give the same bytes.
[[nodiscard]] std::vector<std::uint8_t> serializeRegion(const ModificationStore& store,
                                                        RegionCoord region, std::uint64_t seed);

// The modifications of a region file. Throws StorageError unless bytes are a whole region file
// of a format this build reads, of the world seed and of region, with chunks in order, all in
// the region, and valid records.
[[nodiscard]] ModificationStore deserializeRegion(std::span<const std::uint8_t> bytes,
                                                  RegionCoord region, std::uint64_t seed);

} // namespace terrain
