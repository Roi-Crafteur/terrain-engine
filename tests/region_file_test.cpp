// Region files: regions and groups of negative chunks, canonical names, round trip of every
// region of a world, deterministic and pinned bytes, the files of format 1 still read, and
// rejected files (another world, another region, chunks outside the region or out of order,
// invalid sections, unsorted blocks, no chunk, damaged bytes, newer version).
#include "terrain/storage/region_file.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using storage_test::errorOf;
using storage_test::kSeed;
using terrain::BlockEdit;
using terrain::ChunkCoord;
using terrain::ModificationStore;
using terrain::RegionCoord;
using terrain::StorageErrorKind;

// Offsets in a region file: the body after the 16 bytes of the frame header.
constexpr std::size_t kCountOffset = 32;
constexpr std::size_t kFirstChunk = 36;

std::optional<StorageErrorKind> readError(const std::vector<std::uint8_t>& bytes,
                                          RegionCoord region, std::uint64_t seed = kSeed,
                                          std::string* message = nullptr) {
    return errorOf(
        [&] { static_cast<void>(terrain::deserializeRegion(bytes, region, seed)); }, message);
}

void testRegions() {
    TEST_CHECK(terrain::kRegionChunks == 32);
    TEST_CHECK(terrain::regionOf({0, 0}) == (RegionCoord{0, 0}));
    TEST_CHECK(terrain::regionOf({31, 31}) == (RegionCoord{0, 0}));
    TEST_CHECK(terrain::regionOf({32, -1}) == (RegionCoord{1, -1}));
    TEST_CHECK(terrain::regionOf({-32, -33}) == (RegionCoord{-1, -2}));
    constexpr std::int32_t kLowest = std::numeric_limits<std::int32_t>::min();
    TEST_CHECK(terrain::regionOf({kLowest, 0}) == (RegionCoord{kLowest / 32, 0}));
    TEST_CHECK(terrain::regionGroupOf({-1, 1023}) == (RegionCoord{-1, 31}));
    TEST_CHECK(terrain::regionGroupOf({1024, -1025}) == (RegionCoord{32, -33}));
}

void testNames() {
    TEST_CHECK(terrain::regionRelativePath({-3, 12}) ==
               std::filesystem::path("regions") / "-1.0" / "-3.12.treg");
    TEST_CHECK(terrain::regionRelativePath({40, -2000}) ==
               std::filesystem::path("regions") / "1.-63" / "40.-2000.treg");
    TEST_CHECK(terrain::parseRegionFileName("-3.12.treg") == (RegionCoord{-3, 12}));
    TEST_CHECK(terrain::parseRegionFileName("0.0.treg") == (RegionCoord{0, 0}));
    TEST_CHECK(terrain::parseRegionFileName("2147483647.-2147483648.treg") ==
               (RegionCoord{2147483647, std::numeric_limits<std::int32_t>::min()}));
    TEST_CHECK(terrain::parseRegionGroupName("-1.0") == (RegionCoord{-1, 0}));
    for (const char* bad : {"03.1.treg", "-0.1.treg", "+3.1.treg", "3.1.treg.bak", "3.1.treg.tmp",
                            "3.1", "3..1.treg", "3.1.2.treg", "a.b.treg", "2147483648.0.treg",
                            "3.1.tregx", ".treg", "3.treg", " 3.1.treg", "3.1.TREG"}) {
        TEST_CHECK(!terrain::parseRegionFileName(bad));
    }
    TEST_CHECK(!terrain::parseRegionGroupName("1.0.treg") && !terrain::parseRegionGroupName("x"));
}

void testRoundTrip() {
    const ModificationStore store = storage_test::sampleStore();
    const std::vector<RegionCoord> regions = terrain::regionsOf(store);
    TEST_CHECK(regions ==
               (std::vector<RegionCoord>{{-18, 13}, {-1, 0}, {0, -1}, {1, 1}, {39, -79}}));
    ModificationStore merged;
    for (const RegionCoord region : regions) {
        const std::vector<std::uint8_t> bytes = terrain::serializeRegion(store, region, kSeed);
        ModificationStore part = terrain::deserializeRegion(bytes, region, kSeed);
        TEST_CHECK(!part.empty());
        TEST_CHECK(terrain::serializeRegion(part, region, kSeed) == bytes);
        for (const auto& [coord, chunk] : part.byChunk()) {
            TEST_CHECK(terrain::regionOf(coord) == region && *store.chunk(coord) == chunk);
        }
        merged.merge(std::move(part));
    }
    TEST_CHECK(merged == store);
    TEST_CHECK(terrain::serializeRegion(store, {5, 5}, kSeed).empty());
    TEST_CHECK(terrain::serializeRegion(store, {std::numeric_limits<std::int32_t>::max(), 0}, kSeed)
                   .empty());

    // The same modifications made in another order: the same bytes.
    ModificationStore reordered;
    reordered.addPlant({terrain::PlantType::Cactus, 18.0, -1.0, 2.0f, 0.9f, 4.0f});
    reordered.removePlant(storage_test::tree(17.5, -0.125));
    reordered.addPlant({terrain::PlantType::Boulder, 18.0, -1.0, 2.0f, 0.9f, 4.0f});
    reordered.setBlock({3, 40, -2}, BlockEdit::Added);
    TEST_CHECK(terrain::serializeRegion(reordered, {0, -1}, kSeed) ==
               terrain::serializeRegion(store, {0, -1}, kSeed));
}

// The header of a region file, then its bytes: identical on every platform.
void testPinnedBytes() {
    const std::vector<std::uint8_t> bytes =
        terrain::serializeRegion(storage_test::sampleStore(), {0, -1}, kSeed);
    const std::vector<std::uint8_t> header = {
        'T', 'R', 'E', 'G', 2, 0, 0, 0,             // magic, version
        163, 0, 0, 0, 0, 0, 0, 0,                   // size of the file
        0x9B, 0x28, 0x35, 0x01, 0, 0, 0, 0,         // seed
        0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,         // region (0, -1)
        2, 0, 0, 0,                                 // chunks (0, -1) and (1, -1)
        0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,         // chunk (0, -1)
        15, 0, 0, 0,                                // the size of the rest of its record
        1, 4, 3, 1, 198, 35,                        // section 2, a list of 1 block (index 2275)
        0, 0, 0, 0, 0, 0, 0, 0, 0};                 // no masks, no plants
    TEST_CHECK(bytes.size() == 163);
    TEST_CHECK(std::vector(bytes.begin(), bytes.begin() + 63) == header);
    TEST_CHECK(terrain::fnv1a(bytes) == 0x82BE9434U);
}

// The region files of format 1 are still read, with the checks of their records, and written
// again in the current format.
void testFormatOne() {
    const ModificationStore store = storage_test::sampleStore();
    const std::vector<std::uint8_t> old = storage_test::formatOneRegion(store, {0, -1});
    // The bytes the build of format 1 wrote, pinned then.
    TEST_CHECK(old.size() == 167 && terrain::fnv1a(old) == 0x0C734D3DU);
    const ModificationStore read = terrain::deserializeRegion(old, {0, -1}, kSeed);
    TEST_CHECK(terrain::serializeRegion(read, {0, -1}, kSeed) ==
               terrain::serializeRegion(store, {0, -1}, kSeed));

    ModificationStore blocks;
    blocks.setBlock({1, 0, 1}, BlockEdit::Added);
    blocks.setBlock({2, 0, 1}, BlockEdit::Removed);
    blocks.setBlock({17, 0, 1}, BlockEdit::Added);
    // Chunk (0, 0): coordinates, block count, blocks at 48 and 61 (13 bytes each), then two
    // counts; chunk (1, 0) at 82.
    const std::vector<std::uint8_t> bytes = storage_test::formatOneRegion(blocks, {0, 0});
    TEST_CHECK(bytes.size() == kFirstChunk + 46 + 33 + 4);
    TEST_CHECK(terrain::deserializeRegion(bytes, {0, 0}, kSeed) == blocks);
    std::vector<std::uint8_t> swapped = bytes;
    std::swap_ranges(swapped.begin() + 48, swapped.begin() + 61, swapped.begin() + 61);
    std::string message;
    TEST_CHECK(readError(storage_test::resealed(swapped), {0, 0}, kSeed, &message) ==
               StorageErrorKind::Invalid);
    TEST_CHECK(message.find("out of order") != std::string::npos);
}

void testRejected() {
    const ModificationStore store = storage_test::sampleStore();
    const std::vector<std::uint8_t> bytes = terrain::serializeRegion(store, {0, -1}, kSeed);
    std::string message;
    TEST_CHECK(!readError(bytes, {0, -1}));
    TEST_CHECK(readError(bytes, {0, -1}, kSeed + 1, &message) == StorageErrorKind::Invalid);
    TEST_CHECK(message.find("another world") != std::string::npos);
    TEST_CHECK(readError(bytes, {1, -1}) == StorageErrorKind::Invalid);
    bool damaged = true;
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const std::vector<std::uint8_t> part(bytes.begin(),
                                             bytes.begin() + static_cast<std::ptrdiff_t>(length));
        damaged = damaged && readError(part, {0, -1}) == StorageErrorKind::Truncated;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::vector<std::uint8_t> flipped = bytes;
        flipped[i] ^= 0x01U;
        damaged = damaged && storage_test::isDamage(readError(flipped, {0, -1}));
    }
    TEST_CHECK(damaged);
    std::vector<std::uint8_t> newer = bytes;
    newer[4] = 3;
    TEST_CHECK(readError(storage_test::resealed(newer), {0, -1}) == StorageErrorKind::NewerVersion);
    // No chunk at all.
    std::vector<std::uint8_t> empty(bytes.begin(), bytes.begin() + kFirstChunk);
    storage_test::putU32(empty, kCountOffset, 0);
    empty.resize(empty.size() + 4);
    TEST_CHECK(readError(storage_test::resealed(empty), {0, -1}) == StorageErrorKind::Invalid);
}

// The message of the error a change of bytes then sealed again gives, empty without one.
std::string changedError(std::vector<std::uint8_t> bytes, RegionCoord region) {
    std::string message;
    const std::optional<StorageErrorKind> kind =
        readError(storage_test::resealed(std::move(bytes)), region, kSeed, &message);
    TEST_CHECK(!kind || kind == StorageErrorKind::Invalid);
    return message;
}

// Records changed then sealed again: only the change makes the file invalid.
void testInvalidRecords() {
    ModificationStore store;
    store.setBlock({1, 0, 1}, BlockEdit::Added);
    store.setBlock({2, 0, 1}, BlockEdit::Removed);
    store.setBlock({17, 0, 1}, BlockEdit::Added);
    // Chunk (0, 0): coordinates, the size of the rest at 44, 1 entry at 48: section 0 (49), a
    // list (50) of 2 blocks (51) at 52 and 53, no masks (54), no plants; chunk (1, 0) at 63.
    const std::vector<std::uint8_t> bytes = terrain::serializeRegion(store, {0, 0}, kSeed);
    TEST_CHECK(bytes.size() == kFirstChunk + 27 + 26 + 4);
    TEST_CHECK(!readError(bytes, {0, 0}));
    const auto with = [&bytes](std::size_t at, std::uint8_t value) {
        std::vector<std::uint8_t> changed = bytes;
        changed[at] = value;
        return changed;
    };
    TEST_CHECK(changedError(with(50, 9), {0, 0}).find("unknown kind") != std::string::npos);
    TEST_CHECK(changedError(with(51, 0), {0, 0}).find("invalid list") != std::string::npos);
    TEST_CHECK(changedError(with(51, 17), {0, 0}).find("invalid list") != std::string::npos);
    TEST_CHECK(changedError(with(54, 1), {0, 0}).find("masks") != std::string::npos);
    TEST_CHECK(!changedError(with(44, 16), {0, 0}).empty()); // a record longer than it is
    std::vector<std::uint8_t> longer = with(44, 16);
    longer.insert(longer.begin() + 63, 0);
    TEST_CHECK(changedError(longer, {0, 0}).find("bytes left") != std::string::npos);
    std::vector<std::uint8_t> chunks = bytes;
    chunks.erase(chunks.begin() + 63, chunks.end() - 4);
    chunks.insert(chunks.begin() + kFirstChunk, bytes.begin() + 63, bytes.end() - 4);
    TEST_CHECK(changedError(chunks, {0, 0}).find("out of order") != std::string::npos);
    TEST_CHECK(changedError(with(kFirstChunk, 32), {0, 0}).find("outside the region") !=
               std::string::npos);

    // Two filled sections of one edit, 0 and 2: two runs; made contiguous, they should be one.
    ModificationStore runs;
    runs.setSection({0, 0}, 0, terrain::BlockSection::filled(BlockEdit::Removed));
    runs.setSection({0, 0}, 2, terrain::BlockSection::filled(BlockEdit::Removed));
    const std::vector<std::uint8_t> runBytes = terrain::serializeRegion(runs, {0, 0}, kSeed);
    TEST_CHECK(terrain::deserializeRegion(runBytes, {0, 0}, kSeed) == runs);
    TEST_CHECK(runBytes[48] == 2 && runBytes[52] == 1); // 2 entries, a gap of 1 section
    std::vector<std::uint8_t> joined = runBytes;
    joined[52] = 0;
    TEST_CHECK(changedError(joined, {0, 0}).find("should be one") != std::string::npos);
}

} // namespace

int main() {
    testRegions();
    testNames();
    testRoundTrip();
    testPinnedBytes();
    testFormatOne();
    testRejected();
    testInvalidRecords();
    return test::result();
}
