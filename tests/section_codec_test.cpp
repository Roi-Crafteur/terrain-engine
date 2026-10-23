// Compressed masks of the sections (src/storage/section_codec.hpp): any sections come back bit
// for bit, in order, the bytes read exactly; the bytes are pinned (the same on every platform);
// the typical shapes of the edits cost a few dozen bytes; damaged bytes are detected, never a
// crash; through the region files, sections of up to 16 blocks are lists, the others coded.
#include "section_codec.hpp"

#include "terrain/storage/modifications.hpp"
#include "terrain/storage/region_file.hpp"

#include "test_assert.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

using terrain::BlockEdit;
using terrain::BlockSection;
using terrain::sectionIndex;
using terrain::storage::SectionDecoder;
using terrain::storage::SectionEncoder;

std::uint64_t mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// A section whose block (x, y, z) has the edit of shape(x, y, z).
template <class Shape>
BlockSection sectionOf(Shape shape) {
    BlockSection section;
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                section.set(sectionIndex(x, y, z), shape(x, y, z));
            }
        }
    }
    return section;
}

std::optional<BlockEdit> removedIf(bool inside) {
    return inside ? std::optional(BlockEdit::Removed) : std::nullopt;
}

// A hole of 5 m of radius centered at (cx, cy, cz).
BlockSection sphere(int cx, int cy, int cz) {
    return sectionOf([=](int x, int y, int z) {
        const int dx = x - cx;
        const int dy = y - cy;
        const int dz = z - cz;
        return removedIf(dx * dx + dy * dy + dz * dz <= 25);
    });
}

BlockSection flatCut(int height) {
    return sectionOf([=](int, int y, int) { return removedIf(y >= height); });
}

BlockSection wall(int x0) {
    return sectionOf([=](int x, int, int) { return removedIf(x >= x0); });
}

// Removed, added and generated blocks at random.
BlockSection noise(std::uint64_t seed) {
    return sectionOf([seed](int x, int y, int z) -> std::optional<BlockEdit> {
        switch (mix(seed + static_cast<std::uint64_t>(sectionIndex(x, y, z))) % 3) {
        case 0:
            return std::nullopt;
        case 1:
            return BlockEdit::Removed;
        default:
            return BlockEdit::Added;
        }
    });
}

std::vector<std::uint8_t> encode(const std::vector<BlockSection>& sections) {
    SectionEncoder encoder;
    for (const BlockSection& section : sections) {
        encoder.add(section);
    }
    return encoder.finish();
}

bool decodes(std::span<const std::uint8_t> bytes, const std::vector<BlockSection>& sections) {
    SectionDecoder decoder(bytes);
    bool same = true;
    for (const BlockSection& section : sections) {
        same = same && decoder.next() == section;
    }
    return same && decoder.exact();
}

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes) {
    return terrain::fnv1a(bytes);
}

void testRoundTrip() {
    const std::vector<BlockSection> sections = {
        sphere(7, 7, 7), flatCut(5), BlockSection::filled(BlockEdit::Added), noise(1),
        wall(9), sphere(0, 15, 3), noise(2), flatCut(15)};
    const std::vector<std::uint8_t> bytes = encode(sections);
    TEST_CHECK(decodes(bytes, sections));
    for (const BlockSection& section : sections) {
        TEST_CHECK(decodes(encode({section}), {section}));
    }
    // The same sections, the same bytes, pinned: identical on every platform.
    TEST_CHECK(encode(sections) == bytes);
    std::printf("masks of 8 sections: %zu bytes, FNV-1a 0x%08X\n", bytes.size(), fnv1a(bytes));
    TEST_CHECK(bytes.size() == 1901 && fnv1a(bytes) == 0xE0BAF934U);
}

// The shapes of the edits cost a few dozen bytes; the sections of a column share what they
// learned.
void testSizes() {
    const std::size_t hole = encode({sphere(7, 7, 7)}).size();
    const std::size_t cut = encode({flatCut(5)}).size();
    const std::size_t side = encode({wall(7)}).size();
    const std::size_t column =
        encode({wall(7), wall(7), wall(7), wall(7), wall(7), wall(7), wall(7), wall(7)}).size();
    std::printf("hole of 10 m: %zu bytes, flat cut: %zu, wall: %zu, 8 walls: %zu\n", hole, cut,
                side, column);
    TEST_CHECK(hole <= 80 && cut <= 24 && side <= 24 && column <= 48);
    TEST_CHECK(encode({noise(3)}).size() > 700); // noise does not compress below 1.5 bits
}

void testDamagedBytes() {
    const std::vector<BlockSection> sections = {sphere(4, 9, 11), noise(5)};
    const std::vector<std::uint8_t> bytes = encode(sections);
    const std::vector<std::uint8_t> shorter(bytes.begin(), bytes.end() - 1);
    TEST_CHECK(!decodes(shorter, sections));
    std::vector<std::uint8_t> longer = bytes;
    longer.push_back(0);
    TEST_CHECK(!decodes(longer, sections));
    // Random bytes: some sections, never a crash.
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        std::vector<std::uint8_t> random(static_cast<std::size_t>(mix(seed) % 300));
        for (std::size_t i = 0; i < random.size(); ++i) {
            random[i] = static_cast<std::uint8_t>(mix(seed * 1000 + i));
        }
        SectionDecoder decoder(random);
        static_cast<void>(decoder.next());
        static_cast<void>(decoder.exact());
    }
}

// Through the region files: lists up to 16 blocks, then coded sections, every form read back.
void testRegionForms() {
    terrain::ModificationStore store;
    for (int i = 0; i < 16; ++i) {
        store.setBlock({i, 3, 5}, BlockEdit::Removed);
    }
    for (int i = 0; i < 17; ++i) {
        store.setBlock({16 + i % 16, 3 + i / 16, 5}, BlockEdit::Added);
    }
    store.setSection({0, 1}, -3, sphere(8, 8, 8));
    store.setSection({0, 1}, -2, noise(9));
    store.setSection({0, 1}, 4, BlockSection::filled(BlockEdit::Removed));
    store.setSection({0, 1}, 5, BlockSection::filled(BlockEdit::Added));
    const std::vector<std::uint8_t> bytes = terrain::serializeRegion(store, {0, 0}, 7);
    TEST_CHECK(terrain::deserializeRegion(bytes, {0, 0}, 7) == store);
    TEST_CHECK(terrain::serializeRegion(terrain::deserializeRegion(bytes, {0, 0}, 7), {0, 0}, 7) ==
               bytes);
}

} // namespace

int main() {
    testRoundTrip();
    testSizes();
    testDamagedBytes();
    testRegionForms();
    return test::result();
}
