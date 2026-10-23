// Sections of 16 x 16 x 16 blocks (block_section.hpp) and the store built on them: the same blocks
// always give the same section, whatever the order of the edits (empty, filled, sorted list or
// masks); every query agrees with the blocks one by one; the chunks an edit reaches are those of
// blockChunks.
#include "terrain/storage/block_section.hpp"
#include "terrain/storage/modifications.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::BlockSection;
using terrain::ChunkCoord;
using terrain::kSectionBlocks;
using terrain::SectionMask;

// Deterministic numbers for the random sections.
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }

private:
    std::uint64_t state_;
};

std::optional<BlockEdit> randomEdit(Random& random) {
    switch (random.below(3)) {
    case 0:
        return std::nullopt;
    case 1:
        return BlockEdit::Removed;
    default:
        return BlockEdit::Added;
    }
}

// count random edits, recorded in reference too.
BlockSection randomSection(Random& random, int count,
                           std::vector<std::optional<BlockEdit>>& reference) {
    reference.assign(kSectionBlocks, std::nullopt);
    BlockSection section;
    for (int i = 0; i < count; ++i) {
        const int index = random.below(kSectionBlocks);
        const std::optional<BlockEdit> edit = randomEdit(random);
        section.set(index, edit);
        reference[static_cast<std::size_t>(index)] = edit;
    }
    return section;
}

bool matches(const BlockSection& section, const std::vector<std::optional<BlockEdit>>& reference) {
    int removed = 0;
    int added = 0;
    for (int i = 0; i < kSectionBlocks; ++i) {
        const std::optional<BlockEdit> edit = reference[static_cast<std::size_t>(i)];
        if (section.at(i) != edit) {
            return false;
        }
        removed += edit == BlockEdit::Removed ? 1 : 0;
        added += edit == BlockEdit::Added ? 1 : 0;
    }
    return section.count(BlockEdit::Removed) == removed &&
           section.count(BlockEdit::Added) == added;
}

void testStates() {
    BlockSection section;
    TEST_CHECK(section.empty() && section.count() == 0 && section.heapBytes() == 0);
    TEST_CHECK(!section.at(5) && !section.uniform());
    section.set(5, BlockEdit::Removed);
    section.set(7, BlockEdit::Added);
    TEST_CHECK(section.at(5) == BlockEdit::Removed && section.at(7) == BlockEdit::Added);
    TEST_CHECK(section.count() == 2 && !section.empty());
    section.set(5, std::nullopt);
    section.set(7, std::nullopt);
    TEST_CHECK(section == BlockSection() && section.heapBytes() == 0);

    const BlockSection filled = BlockSection::filled(BlockEdit::Removed);
    TEST_CHECK(filled.uniform() == BlockEdit::Removed && filled.heapBytes() == 0);
    TEST_CHECK(filled.at(0) == BlockEdit::Removed && filled.at(4095) == BlockEdit::Removed);
    BlockSection one = filled;
    one.set(1234, BlockEdit::Added);
    TEST_CHECK(!one.uniform() && one.count(BlockEdit::Removed) == 4095);
    TEST_CHECK(one.at(1234) == BlockEdit::Added && one.at(1233) == BlockEdit::Removed);
    one.set(1234, BlockEdit::Removed);
    TEST_CHECK(one == filled);
    one.set(9, std::nullopt);
    TEST_CHECK(one.count() == 4095 && !one.at(9));

    BlockSection built;
    for (int i = kSectionBlocks - 1; i >= 0; --i) {
        built.set(i, BlockEdit::Added);
    }
    TEST_CHECK(built == BlockSection::filled(BlockEdit::Added) && built.heapBytes() == 0);
}

// The form depends on the blocks only: built in any order, through the masks or not.
void testCanonicalForm() {
    Random random(7);
    for (const int count : {1, 60, 128, 129, 200, 1000, 5000, 20000}) {
        std::vector<std::optional<BlockEdit>> reference;
        const BlockSection section = randomSection(random, count, reference);
        TEST_CHECK(matches(section, reference));
        BlockSection reversed;
        for (int i = kSectionBlocks - 1; i >= 0; --i) {
            reversed.set(i, reference[static_cast<std::size_t>(i)]);
        }
        TEST_CHECK(reversed == section);
        SectionMask removed{};
        SectionMask added{};
        section.toMasks(removed, added);
        TEST_CHECK(BlockSection::fromMasks(removed, added) == section);
        const bool dense = section.count() > BlockSection::kSparseLimit;
        TEST_CHECK(section.heapBytes() == 0 || (dense ? section.heapBytes() >= 1024
                                                      : section.heapBytes() < 1024));
    }
    // Down to the limit of the list and back.
    BlockSection section;
    for (int i = 0; i <= BlockSection::kSparseLimit; ++i) {
        section.set(i * 3, BlockEdit::Removed);
    }
    TEST_CHECK(section.heapBytes() >= 1024);
    section.set(0, std::nullopt);
    TEST_CHECK(section.heapBytes() < 1024 && section.count() == BlockSection::kSparseLimit);
}

void testForEachAndLayers() {
    Random random(11);
    for (const int count : {3, 100, 3000}) {
        std::vector<std::optional<BlockEdit>> reference;
        const BlockSection section = randomSection(random, count, reference);
        int previous = -1;
        bool ordered = true;
        section.forEach([&](int index, BlockEdit edit) {
            ordered = ordered && index > previous &&
                      reference[static_cast<std::size_t>(index)] == edit;
            previous = index;
        });
        TEST_CHECK(ordered);
        terrain::SectionColumns columns{};
        section.addColumns(columns);
        for (int trial = 0; trial < 50; ++trial) {
            const int x0 = random.below(16);
            const int x1 = x0 + random.below(16 - x0);
            const int z0 = random.below(16);
            const int z1 = z0 + random.below(16 - z0);
            std::optional<std::pair<int, int>> expected;
            for (int i = 0; i < kSectionBlocks; ++i) {
                const int x = i % 16;
                const int z = (i / 16) % 16;
                const int y = i / 256;
                if (reference[static_cast<std::size_t>(i)] && x >= x0 && x <= x1 && z >= z0 &&
                    z <= z1) {
                    expected = expected ? std::pair{std::min(expected->first, y),
                                                    std::max(expected->second, y)}
                                        : std::pair{y, y};
                }
            }
            TEST_CHECK(section.editedLayers(x0, x1, z0, z1) == expected);
        }
        bool sameColumns = true;
        for (int i = 0; i < kSectionBlocks; ++i) {
            const auto z = static_cast<std::size_t>((i / 16) % 16);
            const bool column = (columns[z] >> (i % 16) & 1U) != 0;
            sameColumns = sameColumns && (!reference[static_cast<std::size_t>(i)] || column);
        }
        TEST_CHECK(sameColumns);
    }
    const BlockSection filled = BlockSection::filled(BlockEdit::Added);
    TEST_CHECK(filled.editedLayers(3, 4, 5, 6) == (std::pair{0, 15}));
}

void testStore() {
    terrain::ModificationStore store;
    const BlockCoord block{-1, -17, 33};
    store.setBlock(block, BlockEdit::Removed);
    const ChunkCoord chunk = terrain::blockChunk(block);
    TEST_CHECK(chunk == (ChunkCoord{-1, 2}));
    const BlockSection* section = store.section(chunk, -2);
    TEST_CHECK(section != nullptr && section->count() == 1);
    TEST_CHECK(section->at(terrain::sectionIndex(15, 15, 1)) == BlockEdit::Removed);
    TEST_CHECK(store.chunk(chunk)->blocks(chunk) ==
               (std::vector<std::pair<BlockCoord, BlockEdit>>{{block, BlockEdit::Removed}}));

    store.setSection({4, 4}, 3, BlockSection::filled(BlockEdit::Added));
    TEST_CHECK(store.blockAt({64 + 5, 48 + 7, 64 + 9}) == BlockEdit::Added);
    TEST_CHECK(store.blockCount() == 4097);
    store.setSection({4, 4}, 3, BlockSection());
    TEST_CHECK(store.chunk({4, 4}) == nullptr && store.blockCount() == 1);
    TEST_CHECK(store.clearBlock(block) && store.empty());
    TEST_CHECK(!store.clearBlock(block));
}

// reachedChunks merges blockChunks of the blocks of the chunk exactly.
void testReachedChunks() {
    Random random(5);
    for (int trial = 0; trial < 200; ++trial) {
        terrain::ModificationStore store;
        const ChunkCoord chunk{random.below(7) - 3, random.below(7) - 3};
        std::set<std::pair<int, int>> expected;
        const int blocks = 1 + random.below(4);
        for (int i = 0; i < blocks; ++i) {
            const BlockCoord block{chunk.x * 16 + random.below(16), random.below(40) - 20,
                                   chunk.z * 16 + random.below(16)};
            store.setBlock(block, BlockEdit::Added);
            for (const ChunkCoord c : terrain::blockChunks(block)) {
                expected.insert({c.z, c.x});
            }
        }
        std::vector<ChunkCoord> sorted;
        for (const auto& [z, x] : expected) {
            sorted.push_back({x, z});
        }
        TEST_CHECK(terrain::reachedChunks(chunk, *store.chunk(chunk)) == sorted);
    }
}

} // namespace

int main() {
    testStates();
    testCanonicalForm();
    testForEachAndLayers();
    testStore();
    testReachedChunks();
    return test::result();
}
