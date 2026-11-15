// The cost of the edits in the tiles of LOD 0 to 3 (agent 41), timed pair by pair
// (cost_ratio_support.hpp):
// - without an edit within reach, a tile reads no block: the tiles of a world whose edits lie
//   10 km away cost what they cost in a world without edits (at most x1.05, the noise);
// - with 1, 10 and 100 blocks broken in the ground of a tile, its cost against the same tile
//   without them, by LOD: printed, and at most x1.3 (the limit set by the orchestrator).
#include "cost_ratio_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using cost_test::Side;
using terrain::BlockEdit;
using terrain::TerrainTiles;
using terrain::TileKey;

constexpr double kWithoutEdits = 1.05;
constexpr double kWithEdits = 1.3;

// The tiles of every LOD over the point (44, 44), and their neighbors to the east.
std::vector<TileKey> tilesOver() {
    std::vector<TileKey> keys;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const TileKey key = terrain::tileContaining({2, 2}, lod);
        keys.push_back(key);
        keys.push_back({{key.origin.x + terrain::tileChunks(lod), key.origin.z}, lod});
    }
    return keys;
}

// Breaks count blocks in the ground around (44, 44): one, a trench of 10, a hole of 5 x 5 x 4.
void dig(TerrainTiles& tiles, int count, int dx = 0) {
    const auto at = [&tiles, dx](int x, int z, int depth) {
        const int top = static_cast<int>(std::floor(tiles.heightfield().heightAt(x + dx, z)));
        tiles.setBlock({x + dx, top - depth, z}, BlockEdit::Removed);
    };
    if (count == 1) {
        at(44, 44, 0);
    } else if (count == 10) {
        for (int n = 0; n < 10; ++n) {
            at(42 + n % 5, 44 + n / 5, 0);
        }
    } else {
        for (int n = 0; n < 100; ++n) {
            at(42 + n % 5, 42 + (n / 5) % 5, n / 25);
        }
    }
}

void testWithoutEdits() {
    TerrainTiles plain(terrain::kDefaultWorldSeed);
    TerrainTiles far(terrain::kDefaultWorldSeed);
    dig(far, 100, 10'000); // 10 km to the east: no tile here reads them
    const std::vector<TileKey> keys = tilesOver();
    const cost_test::Ratio ratio = cost_test::ratioAtMost(
        keys.size(),
        [&](Side side, std::size_t i) {
            const TerrainTiles& tiles = side == Side::Without ? plain : far;
            return cost_test::secondsOf([&] { (void)tiles.generate({keys[i], 0}); });
        },
        kWithoutEdits);
    std::printf("tiles of LOD 0 to 3 with edits 10 km away: %s\n",
                cost_test::ratioText(ratio).c_str());
    TEST_CHECK(ratio.passed());
    for (const TileKey& key : keys) {
        TEST_CHECK(plain.generate({key, 0}) == far.generate({key, 0}));
    }
}

void testWithEdits() {
    for (const int count : {1, 10, 100}) {
        TerrainTiles plain(terrain::kDefaultWorldSeed);
        TerrainTiles edited(terrain::kDefaultWorldSeed);
        dig(edited, count);
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const std::array<TileKey, 1> keys = {terrain::tileContaining({2, 2}, lod)};
            const cost_test::Ratio ratio = cost_test::ratioAtMost(
                keys.size(),
                [&](Side side, std::size_t i) {
                    const TerrainTiles& tiles = side == Side::Without ? plain : edited;
                    return cost_test::secondsOf([&] { (void)tiles.generate({keys[i], 0}); });
                },
                kWithEdits, {9, 2});
            std::printf("a tile of LOD %d over %d blocks broken: %s\n", lod, count,
                        cost_test::ratioText(ratio).c_str());
            TEST_CHECK(ratio.passed());
        }
    }
}

} // namespace

int main() {
    testWithoutEdits();
    testWithEdits();
    return test::result();
}
