// Volume edits (volume_edit.hpp): a few bytes each, read back exactly or refused; applied by
// sections, they give the store of the same edits made block by block; in any order of arrival,
// the same edits applied by sequence give the same bytes; an explosion is the same everywhere
// (pinned bytes); an edit across an edge of the wrapped world is stored at the canonical image.
#include "terrain/storage/region_file.hpp"
#include "terrain/storage/volume_edit.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::ModificationStore;
using terrain::SequencedEdit;
using terrain::VolumeEdit;
using terrain::WorldSize;

const WorldSize kWorld{};

VolumeEdit box(EditMode mode, BlockCoord anchor, std::uint16_t x, std::uint16_t y,
               std::uint16_t z) {
    return {EditShape::Box, mode, anchor, {x, y, z}, 0};
}

VolumeEdit roundEdit(EditShape shape, EditMode mode, BlockCoord center, double radiusMeters) {
    return {shape, mode, center, {}, static_cast<std::uint16_t>(radiusMeters * 16.0)};
}

// The edit made block by block, as the player would: setBlock or clearBlock.
void applyByBlocks(ModificationStore& store, const VolumeEdit& edit) {
    const terrain::BlockBox bounds = terrain::editBounds(edit);
    for (int y = bounds.min.y; y <= bounds.max.y; ++y) {
        for (int z = bounds.min.z; z <= bounds.max.z; ++z) {
            for (int x = bounds.min.x; x <= bounds.max.x; ++x) {
                const BlockCoord block{x, y, z};
                if (!terrain::contains(edit, block)) {
                    continue;
                }
                if (edit.mode == EditMode::Restore) {
                    store.clearBlock(block);
                } else {
                    store.setBlock(block, edit.mode == EditMode::Add ? BlockEdit::Added
                                                                     : BlockEdit::Removed);
                }
            }
        }
    }
}

// Every region file of the store, in order.
std::vector<std::uint8_t> storeBytes(const ModificationStore& store) {
    std::vector<std::uint8_t> bytes;
    for (const terrain::RegionCoord region : terrain::regionsOf(store)) {
        const std::vector<std::uint8_t> file = terrain::serializeRegion(store, region, 7);
        bytes.insert(bytes.end(), file.begin(), file.end());
    }
    return bytes;
}

bool rejected(const std::vector<std::uint8_t>& bytes) {
    try {
        static_cast<void>(terrain::deserializeVolumeEdit(bytes));
    } catch (const terrain::StorageError& error) {
        return error.kind() == terrain::StorageErrorKind::Invalid;
    }
    return false;
}

void testBytes() {
    const VolumeEdit cube = box(EditMode::Add, {-5, 70, 1 << 20}, 3, 65535, 1);
    const VolumeEdit blast = roundEdit(EditShape::Explosion, EditMode::Remove, {9, -40, -9}, 6.5);
    const std::vector<std::uint8_t> cubeBytes = terrain::serializeVolumeEdit(cube);
    const std::vector<std::uint8_t> blastBytes = terrain::serializeVolumeEdit(blast);
    TEST_CHECK(cubeBytes.size() == terrain::kBoxEditBytes && cubeBytes.size() == 19);
    TEST_CHECK(blastBytes.size() == terrain::kRoundEditBytes && blastBytes.size() == 15);
    TEST_CHECK(cubeBytes[0] == 0x01 && blastBytes[0] == 0x20);
    TEST_CHECK(blastBytes[13] == 104 && blastBytes[14] == 0); // 6.5 m: 104 sixteenths
    TEST_CHECK(terrain::deserializeVolumeEdit(cubeBytes) == cube);
    TEST_CHECK(terrain::deserializeVolumeEdit(blastBytes) == blast);

    std::vector<std::uint8_t> bad = blastBytes;
    bad[0] = 0x21; // an explosion that adds
    TEST_CHECK(rejected(bad));
    bad[0] = 0x30; // unknown shape
    TEST_CHECK(rejected(bad));
    bad[0] = 0x13; // unknown mode
    TEST_CHECK(rejected(bad));
    bad = blastBytes;
    bad[13] = 0; // radius 0
    TEST_CHECK(rejected(bad));
    TEST_CHECK(rejected({blastBytes.begin(), blastBytes.end() - 1}));
    bad = blastBytes;
    bad.push_back(0);
    TEST_CHECK(rejected(bad));
    TEST_CHECK(rejected({cubeBytes.begin(), cubeBytes.begin() + 15}));
}

void testValidation() {
    const auto invalid = [](const VolumeEdit& edit, const WorldSize& world) {
        try {
            terrain::validate(edit, world);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    TEST_CHECK(!invalid(box(EditMode::Remove, {0, 0, 0}, 1, 1, 1), kWorld));
    TEST_CHECK(invalid(box(EditMode::Remove, {0, 0, 0}, 1, 0, 1), kWorld));
    TEST_CHECK(invalid(box(EditMode::Remove, {0, 2147483000, 0}, 1, 1000, 1), kWorld));
    TEST_CHECK(invalid(roundEdit(EditShape::Explosion, EditMode::Restore, {}, 3.0), kWorld));
    TEST_CHECK(invalid(roundEdit(EditShape::Sphere, EditMode::Add, {}, 0.0), kWorld));
    TEST_CHECK(
        invalid(roundEdit(EditShape::Sphere, EditMode::Add, {}, 300.0), WorldSize{512, 4096}));
    ModificationStore store;
    store.setBlock({1, 1, 1}, BlockEdit::Added);
    const ModificationStore before = store;
    const std::vector<SequencedEdit> repeated = {{4, box(EditMode::Remove, {0, 0, 0}, 9, 9, 9)},
                                                 {4, box(EditMode::Add, {0, 0, 0}, 2, 2, 2)}};
    bool threw = false;
    try {
        static_cast<void>(terrain::applyInSequence(store, repeated, kWorld));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw && store == before);
}

// Edits of every shape and mode over earlier edits: by sections, the store of the same edits
// made block by block, and the chunks of their tiles.
void testAgainstBlocks() {
    const std::vector<VolumeEdit> edits = {
        box(EditMode::Remove, {-20, -9, 3}, 50, 40, 37),
        roundEdit(EditShape::Sphere, EditMode::Add, {7, 3, 7}, 9.3),
        roundEdit(EditShape::Explosion, EditMode::Remove, {-16, 16, 30}, 12.0),
        box(EditMode::Restore, {-3, -5, 10}, 9, 30, 9),
        roundEdit(EditShape::Sphere, EditMode::Restore, {0, 0, 16}, 5.0),
        roundEdit(EditShape::Sphere, EditMode::Remove, {64, 64, -64}, 0.5),
        box(EditMode::Add, {-48, 0, -48}, 64, 32, 64)};
    ModificationStore bySections;
    ModificationStore byBlocks;
    for (const VolumeEdit& edit : edits) {
        const std::vector<ChunkCoord> chunks = terrain::applyVolumeEdit(bySections, edit, kWorld);
        applyByBlocks(byBlocks, edit);
        TEST_CHECK(bySections == byBlocks);
        const terrain::BlockBox bounds = terrain::editBounds(edit);
        TEST_CHECK(chunks.front() == terrain::blockChunks(bounds.min).front() &&
                   chunks.back() == terrain::blockChunks(bounds.max).back());
    }
    TEST_CHECK(storeBytes(bySections) == storeBytes(byBlocks));
    // The box of 64 x 32 x 64 blocks on sections: filled, nothing on the heap.
    const terrain::BlockSection* filled = bySections.section({-3, -3}, 1);
    TEST_CHECK(filled != nullptr && filled->uniform() == BlockEdit::Added &&
               filled->heapBytes() == 0);
    terrain::applyVolumeEdit(bySections, box(EditMode::Restore, {-100, -100, -100}, 300, 300, 300),
                             kWorld);
    TEST_CHECK(bySections.empty());
}

// The rough edge of an explosion lies between 90 % and 100 % of its radius.
void testExplosion() {
    const VolumeEdit blast =
        roundEdit(EditShape::Explosion, EditMode::Remove, {100, 40, -300}, 10.0);
    int inside = 0;
    int rough = 0;
    bool edge = true;
    for (int dz = -11; dz <= 11; ++dz) {
        for (int dy = -11; dy <= 11; ++dy) {
            for (int dx = -11; dx <= 11; ++dx) {
                const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                const bool in = terrain::contains(blast, {100 + dx, 40 + dy, -300 + dz});
                edge = edge && (d > 8.98 || in) && (d <= 10.0 || !in);
                inside += in ? 1 : 0;
                rough += (in && d > 9.0) ? 1 : 0;
            }
        }
    }
    TEST_CHECK(edge && rough > 0 && inside > 3000 && inside < 4189);
    ModificationStore store;
    terrain::applyVolumeEdit(store, blast, kWorld);
    const std::vector<std::uint8_t> bytes = storeBytes(store);
    std::printf("explosion of 10 m: %d blocks, %zu bytes, FNV-1a 0x%08X\n", inside, bytes.size(),
                terrain::fnv1a(bytes));
    TEST_CHECK(bytes.size() == 473 && terrain::fnv1a(bytes) == 0xB55311ECU);
}

// The same edits, arriving in any order, applied by sequence: the same bytes.
void testOrder() {
    const std::vector<SequencedEdit> edits = {
        {10, box(EditMode::Remove, {0, 0, 0}, 40, 20, 40)},
        {11, roundEdit(EditShape::Sphere, EditMode::Add, {20, 10, 20}, 8.0)},
        {12, roundEdit(EditShape::Explosion, EditMode::Remove, {25, 12, 18}, 6.0)},
        {13, box(EditMode::Restore, {10, 0, 10}, 4, 30, 4)},
        {20, roundEdit(EditShape::Sphere, EditMode::Add, {0, 0, 0}, 3.0)}};
    ModificationStore reference;
    terrain::applyInSequence(reference, edits, kWorld);
    const std::vector<std::uint8_t> bytes = storeBytes(reference);
    std::vector<SequencedEdit> arrival = edits;
    int permutations = 0;
    bool same = true;
    while (std::next_permutation(arrival.begin(), arrival.end(),
                                 [](const SequencedEdit& a, const SequencedEdit& b) {
                                     return a.sequence < b.sequence;
                                 })) {
        ModificationStore store;
        terrain::applyInSequence(store, arrival, kWorld);
        same = same && storeBytes(store) == bytes;
        ++permutations;
    }
    TEST_CHECK(same && permutations == 119);
    // The order matters: applied in arrival order without the sequence, another world.
    ModificationStore reversed;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        terrain::applyVolumeEdit(reversed, it->edit, kWorld);
    }
    TEST_CHECK(!(reversed == reference));
}

// A sphere across the edge x = world.x / 2, given at two images: stored once, at the canonical
// image, the chunks on both sides of the edge to regenerate.
void testAcrossTheEdge() {
    const ChunkCoord count = terrain::worldChunks(kWorld);
    const int edge = count.x * 8; // world.x / 2
    const VolumeEdit east = roundEdit(EditShape::Sphere, EditMode::Remove, {edge - 2, 5, 100}, 6.0);
    VolumeEdit image = east;
    image.anchor.x -= count.x * 16;
    ModificationStore a;
    ModificationStore b;
    const std::vector<ChunkCoord> chunks = terrain::applyVolumeEdit(a, east, kWorld);
    TEST_CHECK(terrain::applyVolumeEdit(b, image, kWorld) == chunks);
    TEST_CHECK(a == b);
    TEST_CHECK(std::all_of(a.byChunk().begin(), a.byChunk().end(), [](const auto& entry) {
        return terrain::wrapChunk(entry.first, kWorld) == entry.first;
    }));
    TEST_CHECK(a.blockAt({-edge + 2, 5, 100}) == BlockEdit::Removed); // east of the edge
    TEST_CHECK(a.blockAt({edge - 7, 5, 100}) == BlockEdit::Removed);  // west of the edge
    const auto has = [&chunks](ChunkCoord c) {
        return std::find(chunks.begin(), chunks.end(), c) != chunks.end();
    };
    TEST_CHECK(has({count.x / 2 - 1, 6}) && has({-count.x / 2, 6}));
    TEST_CHECK(std::all_of(chunks.begin(), chunks.end(),
                           [](ChunkCoord c) { return terrain::wrapChunk(c, kWorld) == c; }));
}

} // namespace

int main() {
    testBytes();
    testValidation();
    testAgainstBlocks();
    testExplosion();
    testOrder();
    testAcrossTheEdge();
    return test::result();
}
