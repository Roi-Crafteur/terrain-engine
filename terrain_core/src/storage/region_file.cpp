#include "terrain/storage/region_file.hpp"

#include "byte_stream.hpp"
#include "storage_codec.hpp"
#include "terrain/storage/world_meta.hpp"

#include <charconv>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace terrain {

namespace {

constexpr storage::Magic kMagic = {'T', 'R', 'E', 'G'};
constexpr std::string_view kExtension = ".treg";
// The body before the chunks: seed, coordinates and chunk count.
constexpr std::size_t kBodyHeader = 20;

std::string nameText(RegionCoord coord) {
    return std::to_string(coord.x) + "." + std::to_string(coord.z);
}

std::string regionText(RegionCoord coord) {
    return "region (" + std::to_string(coord.x) + ", " + std::to_string(coord.z) + ")";
}

// A canonical decimal int32, as std::to_string writes it.
std::optional<std::int32_t> parseInt(std::string_view text) {
    std::int32_t value = 0;
    const char* end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || last != end || std::to_string(value) != text) {
        return std::nullopt;
    }
    return value;
}

std::optional<RegionCoord> parsePair(std::string_view text) {
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<std::int32_t> x = parseInt(text.substr(0, dot));
    const std::optional<std::int32_t> z = parseInt(text.substr(dot + 1));
    if (!x || !z) {
        return std::nullopt;
    }
    return RegionCoord{*x, *z};
}

// Calls visit(coord, modifications) for each chunk of store in region, by z then x. A region
// beyond the 32-bit chunk coordinates holds none.
template <class Visit>
void forEachChunk(const ModificationStore& store, RegionCoord region, Visit visit) {
    constexpr std::int32_t kLowest = std::numeric_limits<std::int32_t>::min() >> kRegionShift;
    constexpr std::int32_t kHighest = std::numeric_limits<std::int32_t>::max() >> kRegionShift;
    if (region.x < kLowest || region.x > kHighest || region.z < kLowest || region.z > kHighest) {
        return;
    }
    const auto& chunks = store.byChunk();
    const std::int32_t x0 = region.x * kRegionChunks;
    const std::int32_t z0 = region.z * kRegionChunks;
    for (int dz = 0; dz < kRegionChunks; ++dz) {
        const std::int32_t z = z0 + dz;
        for (auto it = chunks.lower_bound({x0, z});
             it != chunks.end() && it->first.z == z && regionOf(it->first).x == region.x; ++it) {
            visit(it->first, it->second);
        }
    }
}

} // namespace

RegionCoord regionOf(ChunkCoord chunk) {
    return {chunk.x >> kRegionShift, chunk.z >> kRegionShift};
}

RegionCoord regionGroupOf(RegionCoord region) {
    return {region.x >> kRegionGroupShift, region.z >> kRegionGroupShift};
}

std::filesystem::path regionRelativePath(RegionCoord region) {
    return std::filesystem::path(kRegionDirectoryName) / nameText(regionGroupOf(region)) /
           (nameText(region) + std::string(kExtension));
}

std::optional<RegionCoord> parseRegionGroupName(std::string_view name) {
    return parsePair(name);
}

std::optional<RegionCoord> parseRegionFileName(std::string_view name) {
    if (!name.ends_with(kExtension)) {
        return std::nullopt;
    }
    return parsePair(name.substr(0, name.size() - kExtension.size()));
}

std::vector<RegionCoord> regionsOf(const ModificationStore& store) {
    std::set<RegionCoord> regions;
    for (const auto& [coord, chunk] : store.byChunk()) {
        regions.insert(regionOf(coord));
    }
    return {regions.begin(), regions.end()};
}

std::vector<std::uint8_t> serializeRegion(const ModificationStore& store, RegionCoord region,
                                          std::uint64_t seed) {
    std::size_t count = 0;
    std::size_t size = kBodyHeader;
    forEachChunk(store, region, [&](ChunkCoord, const ChunkModifications& chunk) {
        ++count;
        size += storage::chunkRecordSize(chunk);
    });
    if (count == 0) {
        return {};
    }
    storage::ByteWriter out = storage::beginFrame(kMagic, kWorldFormatVersion, size);
    out.u64(seed);
    out.i32(region.x);
    out.i32(region.z);
    out.count(count);
    forEachChunk(store, region, [&out](ChunkCoord coord, const ChunkModifications& chunk) {
        storage::writeChunk(out, coord, chunk);
    });
    return storage::sealFrame(std::move(out));
}

ModificationStore deserializeRegion(std::span<const std::uint8_t> bytes, RegionCoord region,
                                    std::uint64_t seed) {
    const std::string what = regionText(region);
    const storage::Frame frame = storage::openFrame(bytes, kMagic, kWorldFormatVersion, what);
    storage::ByteReader in(frame.body, "invalid " + what + ": ");
    const std::uint64_t fileSeed = in.u64();
    if (fileSeed != seed) {
        in.fail("belongs to another world (seed " + std::to_string(fileSeed) + ", not " +
                std::to_string(seed) + ")");
    }
    RegionCoord stored;
    stored.x = in.i32();
    stored.z = in.i32();
    if (stored != region) {
        in.fail("holds the " + regionText(stored));
    }
    const std::size_t count = in.count(storage::kMinChunkRecord);
    if (count == 0 || count > kMaxRegionChunks) {
        in.fail(std::to_string(count) + " chunks, 1 to 1024 expected");
    }
    ModificationStore store;
    std::optional<ChunkCoord> previous;
    for (std::size_t i = 0; i < count; ++i) {
        const ChunkCoord coord = storage::readChunkCoord(in);
        if (regionOf(coord) != region) {
            in.fail("chunk outside the region");
        }
        if (previous && !ChunkOrder{}(*previous, coord)) {
            in.fail("chunks out of order or repeated");
        }
        previous = coord;
        storage::readChunkBody(in, coord, store);
    }
    in.expectEnd("trailing bytes");
    return store;
}

} // namespace terrain
