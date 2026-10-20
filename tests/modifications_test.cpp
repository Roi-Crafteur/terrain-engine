// Modifications: blocks stored per chunk, the chunks a block reaches, local and order-free
// density edits, plant edits identified by position, and the modification file (round trip,
// deterministic bytes pinned on a reference file, FNV-1a checksum, rejected corrupt files, atomic
// save).
#include "terrain/storage/modifications.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::ChunkCoord;
using terrain::ModificationStore;
using terrain::Plant;
using terrain::PlantType;

constexpr std::uint64_t kSeed = 20261019;

void testBlocks() {
    ModificationStore store;
    TEST_CHECK(store.empty() && !store.blockAt({1, 2, 3}));
    store.setBlock({1, 2, 3}, BlockEdit::Added);
    store.setBlock({-1, 5, -17}, BlockEdit::Removed);
    TEST_CHECK(store.blockAt({1, 2, 3}) == BlockEdit::Added);
    TEST_CHECK(store.blockAt({-1, 5, -17}) == BlockEdit::Removed);
    TEST_CHECK(!store.blockAt({1, 3, 3}));
    TEST_CHECK(terrain::blockChunk({-1, 5, -17}) == (ChunkCoord{-1, -2}));
    TEST_CHECK(store.chunk({-1, -2}) != nullptr && store.chunk({0, 0}) != nullptr);
    TEST_CHECK(store.blockCount() == 2);

    store.setBlock({1, 2, 3}, BlockEdit::Removed); // replaces the edit
    TEST_CHECK(store.blockAt({1, 2, 3}) == BlockEdit::Removed && store.blockCount() == 2);
    TEST_CHECK(store.clearBlock({1, 2, 3}) && !store.clearBlock({1, 2, 3}));
    TEST_CHECK(store.chunk({0, 0}) == nullptr); // an empty chunk keeps no entry
    TEST_CHECK(store.clearBlock({-1, 5, -17}) && store.empty());
}

// The chunks of a block: brute force over the lattice points it reaches.
void testBlockChunks() {
    TEST_CHECK(terrain::blockChunks({5, 0, 5}) == (std::vector<ChunkCoord>{{0, 0}}));
    TEST_CHECK(terrain::blockChunks({16, 9, 5}) == (std::vector<ChunkCoord>{{0, 0}, {1, 0}}));
    TEST_CHECK(terrain::blockChunks({-1, 0, 0}) ==
               (std::vector<ChunkCoord>{{-1, -1}, {0, -1}, {-1, 0}, {0, 0}}));
    bool matches = true;
    for (const int x : {-33, -17, -16, -15, -1, 0, 1, 14, 15, 16, 17, 31, 32, 47}) {
        const BlockCoord block{x, 0, 7};
        std::vector<int> expected;
        for (int n = -4; n <= 4; ++n) {
            for (int p = x - terrain::kBlockReach; p <= x + terrain::kBlockReach; ++p) {
                if (p >= 16 * n && p <= 16 * n + 16) {
                    expected.push_back(n);
                    break;
                }
            }
        }
        std::vector<int> actual;
        for (const ChunkCoord c : terrain::blockChunks(block)) {
            actual.push_back(c.x);
            matches = matches && c.z == 0;
        }
        matches = matches && actual == expected;
    }
    TEST_CHECK(matches);
}

void testBlockDensity() {
    const BlockCoord b{10, 20, 30};
    TEST_CHECK(terrain::blockDistance(b, 10, 20, 30) == -0.5f);
    TEST_CHECK(terrain::blockDistance(b, 11, 20, 30) == 0.5f);
    TEST_CHECK(std::abs(terrain::blockDistance(b, 11, 21, 30) - std::sqrt(0.5f)) < 1e-6f);
    TEST_CHECK(std::abs(terrain::blockDistance(b, 9, 19, 31) - std::sqrt(0.75f)) < 1e-6f);
    TEST_CHECK(terrain::blockDistance(b, 12, 20, 30) == 1.5f);

    // Added: air becomes rock at the center; removed: rock becomes air.
    TEST_CHECK(terrain::editDensity(0.3f, b, BlockEdit::Added, 10, 20, 30) == -0.5f);
    TEST_CHECK(terrain::editDensity(-3.0f, b, BlockEdit::Removed, 10, 20, 30) == 0.5f);
    TEST_CHECK(terrain::editDensity(-3.0f, b, BlockEdit::Removed, 11, 20, 30) == -0.5f);
    // Beyond one meter (Chebyshev), nothing changes.
    TEST_CHECK(terrain::editDensity(9.0f, b, BlockEdit::Added, 12, 20, 30) == 9.0f);
    TEST_CHECK(terrain::editDensity(-9.0f, b, BlockEdit::Removed, 10, 22, 30) == -9.0f);
}

// Added blocks first, then removed ones, whatever order the edits were made in.
void testEditOrderDoesNotMatter() {
    const BlockCoord added{0, 0, 0};
    const BlockCoord removed{1, 0, 0};
    ModificationStore first;
    first.setBlock(added, BlockEdit::Added);
    first.setBlock(removed, BlockEdit::Removed);
    ModificationStore second;
    second.setBlock(removed, BlockEdit::Removed);
    second.setBlock(added, BlockEdit::Added);
    bool same = true;
    bool rule = true;
    for (int z = -2; z <= 2; ++z) {
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 3; ++x) {
                const float base = 0.25f * static_cast<float>(y) - 0.1f;
                const float a = first.editedDensity(base, x, y, z);
                same = same && a == second.editedDensity(base, x, y, z);
                const float expected = terrain::editDensity(
                    terrain::editDensity(base, added, BlockEdit::Added, x, y, z), removed,
                    BlockEdit::Removed, x, y, z);
                rule = rule && a == expected;
            }
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(rule);
    const terrain::BlockEdits edits = first.blocksIn(-1, 0, -1, 1);
    TEST_CHECK(edits.added == std::vector<BlockCoord>{added} && edits.removed.empty());
}

Plant tree(double x, double z) {
    return {PlantType::Tree, x, z, 12.5f, 1.1f, 0.3f};
}

void testPlants() {
    ModificationStore store;
    const Plant generated = tree(-3.25, 40.75);
    TEST_CHECK(!store.isRemoved(generated));
    store.removePlant(generated); // not an added plant: recorded as removed
    TEST_CHECK(store.isRemoved(generated) && store.removedPlantCount() == 1);
    TEST_CHECK(store.chunk({-1, 2}) != nullptr);
    // Identified by its exact position.
    TEST_CHECK(!store.isRemoved(tree(-3.25 + 1e-9, 40.75)));

    const Plant added{PlantType::Flower, -5.0, 33.0, 9.0f, 0.8f, 1.0f};
    store.addPlant(added);
    TEST_CHECK(store.addedPlantCount() == 1);
    std::vector<Plant> plants = {generated, tree(-8.0, 36.0)};
    store.applyPlantEdits({-1, 2}, plants);
    TEST_CHECK(plants == (std::vector<Plant>{tree(-8.0, 36.0), added}));

    store.removePlant(added); // an added plant is erased, not recorded
    TEST_CHECK(store.addedPlantCount() == 0 && store.removedPlantCount() == 1);
    TEST_CHECK(!store.isRemoved(added));
}

ModificationStore sampleStore() {
    ModificationStore store;
    store.setBlock({3, 40, -2}, BlockEdit::Added);
    store.setBlock({-20, -7, 100}, BlockEdit::Removed);
    store.setBlock({-21, -7, 100}, BlockEdit::Removed);
    store.removePlant(tree(17.5, -0.125));
    store.removePlant(tree(-100.0625, 3.0));
    store.addPlant({PlantType::Cactus, 18.0, -1.0, 2.0f, 0.9f, 4.0f});
    store.addPlant({PlantType::Boulder, 18.0, -1.0, 2.0f, 0.9f, 4.0f}); // same spot: allowed
    return store;
}

std::uint32_t trailer(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[bytes.size() - 4 + static_cast<std::size_t>(i)])
                 << (8 * i);
    }
    return value;
}

void testFnv1a() {
    const auto hash = [](std::string_view text) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
        return terrain::fnv1a({data, text.size()});
    };
    TEST_CHECK(hash("") == 0x811C9DC5U);
    TEST_CHECK(hash("a") == 0xE40C292CU);
    TEST_CHECK(hash("foobar") == 0xBF9CF968U);
}

void testRoundTrip() {
    const ModificationStore store = sampleStore();
    const std::vector<std::uint8_t> bytes = terrain::serializeModifications(store, kSeed);
    TEST_CHECK(bytes.size() > 8 && std::equal(bytes.begin(), bytes.begin() + 4, "TMOD"));
    TEST_CHECK(trailer(bytes) ==
               terrain::fnv1a(std::span(bytes).first(bytes.size() - 4)));
    const terrain::SavedModifications saved = terrain::deserializeModifications(bytes);
    TEST_CHECK(saved.seed == kSeed && saved.store == store);
    TEST_CHECK(terrain::serializeModifications(saved.store, kSeed) == bytes); // deterministic

    const auto empty = terrain::deserializeModifications(terrain::serializeModifications({}, 7));
    TEST_CHECK(empty.seed == 7 && empty.store.empty());
}

// The file of sampleStore() for kSeed, byte for byte: the format of every saved TMOD file. Any
// change of the code that changes these bytes breaks the existing saves.
const std::vector<std::uint8_t> kReferenceFile = {
    // header: magic, version 1, seed, 4 chunks
    0x54, 0x4D, 0x4F, 0x44, 0x01, 0x00, 0x00, 0x00, 0x9B, 0x28, 0x35, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    // chunk (0, -1): block (3, 40, -2) added
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // chunk (1, -1): a removed plant, a cactus and a boulder
    0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x31, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xBF, 0x02, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xF0, 0xBF, 0x00, 0x00, 0x00, 0x40, 0x66, 0x66, 0x66,
    0x3F, 0x00, 0x00, 0x80, 0x40, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x32, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xBF, 0x00, 0x00,
    0x00, 0x40, 0x66, 0x66, 0x66, 0x3F, 0x00, 0x00, 0x80, 0x40,
    // chunk (-7, 0): a removed plant
    0xF9, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0xC0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x40, 0x00, 0x00, 0x00, 0x00,
    // chunk (-2, 6): blocks (-21, -7, 100) and (-20, -7, 100) removed
    0xFE, 0xFF, 0xFF, 0xFF, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0xEB, 0xFF, 0xFF, 0xFF, 0xF9, 0xFF, 0xFF, 0xFF, 0x64, 0x00, 0x00, 0x00,
    0x01, 0xEC, 0xFF, 0xFF, 0xFF, 0xF9, 0xFF, 0xFF, 0xFF, 0x64, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // FNV-1a
    0x7B, 0x2B, 0xEB, 0x4E,
};

void testReferenceBytes() {
    TEST_CHECK(terrain::serializeModifications(sampleStore(), kSeed) == kReferenceFile);
    const terrain::SavedModifications saved = terrain::deserializeModifications(kReferenceFile);
    TEST_CHECK(saved.seed == kSeed && saved.store == sampleStore());
}

bool rejected(const std::vector<std::uint8_t>& bytes) {
    try {
        static_cast<void>(terrain::deserializeModifications(bytes));
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// Recomputes the checksum, so that only the change under test makes the file invalid.
std::vector<std::uint8_t> resealed(std::vector<std::uint8_t> bytes) {
    bytes.resize(bytes.size() - 4);
    const std::uint32_t hash = terrain::fnv1a(bytes);
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((hash >> (8 * i)) & 0xFFU));
    }
    return bytes;
}

void testCorruptFilesRejected() {
    const std::vector<std::uint8_t> good = terrain::serializeModifications(sampleStore(), kSeed);
    TEST_CHECK(!rejected(good));
    for (std::size_t i = 0; i < good.size(); ++i) {
        std::vector<std::uint8_t> flipped = good;
        flipped[i] ^= 0x10U;
        TEST_CHECK(rejected(flipped)); // any changed byte breaks the checksum
    }
    TEST_CHECK(rejected({good.begin(), good.end() - 1}));  // truncated
    TEST_CHECK(rejected({}));
    std::vector<std::uint8_t> version = good;
    version[4] = 2;
    TEST_CHECK(rejected(resealed(version)));
    std::vector<std::uint8_t> trailing = good;
    trailing.insert(trailing.end() - 4, 0);
    TEST_CHECK(rejected(resealed(trailing)));
    // First chunk: (0, -1) holds the block (3, 40, -2), its x at byte 32 after the header (20),
    // the chunk coordinates (8) and the block count (4). Moved to x = 30, it leaves its chunk.
    std::vector<std::uint8_t> misplaced = good;
    TEST_CHECK(good[32] == 3);
    misplaced[32] = 30;
    TEST_CHECK(rejected(resealed(misplaced)));
    std::vector<std::uint8_t> badEdit = good;
    badEdit[44] = 7; // the edit of that block
    TEST_CHECK(rejected(resealed(badEdit)));
}

std::filesystem::path testDirectory() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "terrain_engine_modifications_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void testSaveAndLoad() {
    const std::filesystem::path dir = testDirectory();
    const std::filesystem::path file = dir / "terrain_modifications.bin";
    const ModificationStore store = sampleStore();
    terrain::saveModifications(file, store, kSeed);
    TEST_CHECK(std::filesystem::exists(file));
    TEST_CHECK(!std::filesystem::exists(dir / "terrain_modifications.bin.tmp"));
    const terrain::SavedModifications loaded = terrain::loadModifications(file);
    TEST_CHECK(loaded.seed == kSeed && loaded.store == store);

    std::ifstream in(file, std::ios::binary);
    const std::vector<std::uint8_t> onDisk((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
    TEST_CHECK(onDisk == terrain::serializeModifications(store, kSeed));

    // Saving again replaces the file.
    ModificationStore more = store;
    more.setBlock({0, 0, 0}, BlockEdit::Added);
    terrain::saveModifications(file, more, kSeed);
    TEST_CHECK(terrain::loadModifications(file).store == more);

    bool missingThrows = false;
    try {
        static_cast<void>(terrain::loadModifications(dir / "missing.bin"));
    } catch (const std::runtime_error&) {
        missingThrows = true;
    }
    TEST_CHECK(missingThrows);
    // A failed save leaves no file behind.
    bool saveThrows = false;
    try {
        terrain::saveModifications(dir / "no_such_dir" / "file.bin", store, kSeed);
    } catch (const std::runtime_error&) {
        saveThrows = true;
    }
    TEST_CHECK(saveThrows && !std::filesystem::exists(dir / "no_such_dir"));
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    testBlocks();
    testBlockChunks();
    testBlockDensity();
    testEditOrderDoesNotMatter();
    testPlants();
    testFnv1a();
    testRoundTrip();
    testReferenceBytes();
    testCorruptFilesRejected();
    testSaveAndLoad();
    return test::result();
}
