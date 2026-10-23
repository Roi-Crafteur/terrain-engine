// Edit windows (edit_window.hpp): the density of a window is the density of the edits block by
// block (ModificationStore::editedDensity) bit for bit; the lattice of a tile gives every sample
// the density of the window at its point, for every spacing of the LOD; the spans, the filled
// volumes and the wrapped world are read as the store holds them; the plants standing on a
// removed block are dropped, whatever the area of the window.
#include "terrain/storage/edit_window.hpp"
#include "terrain/storage/volume_edit.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::EditLattice;
using terrain::EditMode;
using terrain::EditShape;
using terrain::EditWindow;
using terrain::ModificationStore;
using terrain::VolumeEdit;
using terrain::WorldSize;

const WorldSize kWorld{};

class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    int below(int n) {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return static_cast<int>((z ^ (z >> 31)) % static_cast<std::uint64_t>(n));
    }

private:
    std::uint64_t state_;
};

// Blocks added and removed at random around the origin, sections filled, a sphere.
ModificationStore randomStore(std::uint64_t seed) {
    Random random(seed);
    ModificationStore store;
    for (int i = 0; i < 3000; ++i) {
        const BlockCoord block{random.below(40) - 20, random.below(40) - 20, random.below(40) - 20};
        store.setBlock(block, random.below(2) == 0 ? BlockEdit::Added : BlockEdit::Removed);
    }
    store.setSection({-2, 0}, 0, terrain::BlockSection::filled(BlockEdit::Removed));
    store.setSection({0, -2}, -1, terrain::BlockSection::filled(BlockEdit::Added));
    terrain::applyVolumeEdit(store, {EditShape::Sphere, EditMode::Remove, {5, 3, -6}, {}, 120},
                             kWorld);
    return store;
}

float generated(int x, int y, int z) {
    return static_cast<float>(y) * 0.37f - static_cast<float>(x - z) * 0.11f;
}

bool sameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// The edits of the blocks within reach of (x, y, z), block by block with editDensity: every
// added block, then every removed one.
float byBlocks(const ModificationStore& store, float density, int x, int y, int z) {
    for (const BlockEdit pass : {BlockEdit::Added, BlockEdit::Removed}) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const BlockCoord block{x + dx, y + dy, z + dz};
                    if (store.blockAt(block) == pass) {
                        density = terrain::editDensity(density, block, pass, x, y, z);
                    }
                }
            }
        }
    }
    return density;
}

void testDensity() {
    const ModificationStore store = randomStore(3);
    const EditWindow window(store, -40, 40, -40, 40, kWorld);
    bool same = true;
    for (int z = -26; z <= 26; ++z) {
        for (int y = -26; y <= 26; ++y) {
            for (int x = -26; x <= 26; ++x) {
                const float d = generated(x, y, z);
                same = same && sameBits(window.density(d, x, y, z), byBlocks(store, d, x, y, z));
            }
        }
    }
    // And the density of the store itself, on a few points.
    Random random(17);
    for (int i = 0; i < 300; ++i) {
        const int x = random.below(50) - 25;
        const int y = random.below(50) - 25;
        const int z = random.below(50) - 25;
        const float d = generated(x, y, z);
        same = same && sameBits(window.density(d, x, y, z), store.editedDensity(d, x, y, z));
    }
    TEST_CHECK(same);
    TEST_CHECK(window.blockAt(-30, 5, 2) == BlockEdit::Removed);
    TEST_CHECK(!EditWindow(store, 100, 120, 100, 120, kWorld).blockAt(-30, 5, 2));
}

// The samples of every spacing of the LOD, at origins on and off the sections.
void testLattice() {
    const ModificationStore store = randomStore(8);
    int edited = 0;
    for (const int spacing : {1, 2, 4, 8}) {
        for (const BlockCoord origin : {BlockCoord{-32, -16, -32}, BlockCoord{-24, -40, 8}}) {
            const int side = 16 * spacing;
            const EditWindow window(store, origin.x - 1, origin.x + side + 1, origin.z - 1,
                                    origin.z + side + 1, kWorld);
            const EditLattice lattice(window, origin.x, origin.y, origin.z, spacing, 17);
            bool same = true;
            for (int k = 0; k < 17; ++k) {
                for (int j = 0; j < 17; ++j) {
                    for (int i = 0; i < 17; ++i) {
                        const int x = origin.x + i * spacing;
                        const int y = origin.y + j * spacing;
                        const int z = origin.z + k * spacing;
                        const float d = generated(x, y, z);
                        same = same && sameBits(lattice.density(d, i, j, k),
                                                window.density(d, x, y, z));
                    }
                }
            }
            TEST_CHECK(same);
            edited += lattice.empty() ? 0 : 1;
        }
    }
    TEST_CHECK(edited >= 6);
    const EditWindow far(store, 1000, 1100, 1000, 1100, kWorld);
    TEST_CHECK(far.empty() && EditLattice(far, 1000, 0, 1000, 4, 17).empty());
}

// The spans are those of the blocks of the area, section by section.
void testSpans() {
    ModificationStore store;
    store.setBlock({3, 5, 3}, BlockEdit::Added);
    store.setBlock({3, 12, 3}, BlockEdit::Removed);
    store.setBlock({20, 40, 3}, BlockEdit::Removed); // outside the area below
    store.setBlock({3, -1, 15}, BlockEdit::Added);
    store.setSection({0, 0}, 4, terrain::BlockSection::filled(BlockEdit::Removed));
    const EditWindow window(store, 0, 18, 0, 15, kWorld);
    std::vector<std::pair<int, int>> spans = window.editedSpans();
    std::sort(spans.begin(), spans.end());
    TEST_CHECK(spans == (std::vector<std::pair<int, int>>{{-1, -1}, {5, 12}, {64, 79}}));
    TEST_CHECK(window.filled({1, 65, 1}, {14, 78, 14}));
    TEST_CHECK(!window.filled({1, 60, 1}, {14, 78, 14}));
    TEST_CHECK(!window.filled({1, 65, 1}, {20, 78, 14}));
}

// An area across the edge x = world.x / 2 reads the canonical chunks, in its own frame.
void testAcrossTheEdge() {
    const int edge = terrain::worldChunks(kWorld).x * 8;
    ModificationStore store;
    store.setBlock({-edge + 1, 7, 4}, BlockEdit::Removed); // canonical, just east of the edge
    const EditWindow window(store, edge - 8, edge + 8, 0, 8, kWorld);
    TEST_CHECK(window.blockAt(edge + 1, 7, 4) == BlockEdit::Removed);
    TEST_CHECK(!window.blockAt(-edge + 1, 7, 4)); // outside the area of the window
    TEST_CHECK(window.density(-3.0f, edge + 1, 7, 4) == 0.5f);
}

void testPlants() {
    ModificationStore store;
    store.setBlock({10, 4, 7}, BlockEdit::Removed);  // the ground under the first plant
    store.setBlock({20, 6, 7}, BlockEdit::Removed);  // the air above the second
    store.setBlock({30, 4, 7}, BlockEdit::Added);    // a block built at the foot of the third
    const terrain::Plant onHole{terrain::PlantType::Tree, 10.3, 6.6, 4.7f, 1.0f, 0.0f};
    const terrain::Plant underAir{terrain::PlantType::Tree, 19.6, 7.2, 5.1f, 1.0f, 0.0f};
    const terrain::Plant onBuilt{terrain::PlantType::Bush, 29.9, 7.4, 4.2f, 1.0f, 0.0f};
    TEST_CHECK(terrain::plantFootBlock(onHole) == (BlockCoord{10, 4, 7}));
    TEST_CHECK(terrain::plantFootBlock(underAir) == (BlockCoord{20, 5, 7}));
    // The window of a chunk and of a far tile over it give the same plants.
    for (const int side : {16, 128}) {
        std::vector<terrain::Plant> plants = {onHole, underAir, onBuilt};
        terrain::dropPlantsOnRemovedGround(EditWindow(store, -1, side + 1, -1, side + 1, kWorld),
                                           plants);
        TEST_CHECK(plants == (std::vector<terrain::Plant>{underAir, onBuilt}));
    }
}

} // namespace

int main() {
    testDensity();
    testLattice();
    testSpans();
    testAcrossTheEdge();
    testPlants();
    return test::result();
}
