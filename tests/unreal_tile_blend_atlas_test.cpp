// The surface blend of the tiles for the materials of Unreal (Bridge/tile_blend_atlas): its
// texels exact in floats (every id and weight of every texel of the core comes back), the region
// of a tile in the order of the texture, the pitch of the texels per LOD, and the slots of the
// atlas (kept, given again lowest first, full, cleared, their corners).
#include "tile_blend_atlas.hpp"

#include "test_assert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

using terrain::BlendTexel;
using terrain::TileKey;
using terrain::unreal::BlendAtlas;

template <class Error, class Call>
bool throws(Call call) {
    try {
        call();
    } catch (const Error&) {
        return true;
    }
    return false;
}

// Texels of every field: the largest ids, weights with both halves set, and the blend of a core
// column.
std::vector<BlendTexel> sampleTexels() {
    terrain::SurfaceBlend blend;
    blend.biomes = {terrain::Biome::TemperateForest, terrain::Biome::Grassland,
                    terrain::Biome::TemperateForest};
    blend.biomeWeights = {0.6f, 0.4f, 0.0f};
    blend.materials = {terrain::SurfaceMaterial::Rock, terrain::SurfaceMaterial::Sand,
                       terrain::SurfaceMaterial::Pebbles};
    blend.materialWeights = {0.5f, 0.3f, 0.2f};
    return {BlendTexel{}, BlendTexel{0xFFFFFFu, 0xFFFFFFFFu}, BlendTexel{0x123456u, 0x80017FFEu},
            BlendTexel{0xABCDEFu, 0x00FF00FFu}, terrain::packBlend(blend)};
}

void testTexelFloats() {
    bool exact = true;
    for (const BlendTexel& texel : sampleTexels()) {
        const std::array<float, 4> floats = terrain::unreal::blendTexelFloats(texel);
        exact = exact && floats[3] == 1.0f;
        const std::optional<BlendTexel> back = terrain::unreal::blendTexelOf(floats);
        exact = exact && back.has_value() && *back == texel;
    }
    TEST_CHECK(exact);
    // The halves of the weights as the shader reads them: low 16 bits, then high 16 bits.
    const std::array<float, 4> floats =
        terrain::unreal::blendTexelFloats(BlendTexel{0x000102u, 0x00030004u});
    TEST_CHECK(floats[0] == 258.0f && floats[1] == 4.0f && floats[2] == 3.0f);
    // The texels of no tile (all 0) and broken floats are no texel.
    TEST_CHECK(!terrain::unreal::blendTexelOf({0.0f, 0.0f, 0.0f, 0.0f}));
    TEST_CHECK(!terrain::unreal::blendTexelOf({0.5f, 0.0f, 0.0f, 1.0f}));
    TEST_CHECK(!terrain::unreal::blendTexelOf({-1.0f, 0.0f, 0.0f, 1.0f}));
    TEST_CHECK(!terrain::unreal::blendTexelOf({0.0f, 65536.0f, 0.0f, 1.0f}));
    TEST_CHECK(!terrain::unreal::blendTexelOf(
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f}));
}

void testRegion() {
    constexpr int side = terrain::kTileBlendTexels;
    std::vector<BlendTexel> texels;
    for (int k = 0; k < side; ++k) {
        for (int i = 0; i < side; ++i) {
            texels.push_back(BlendTexel{static_cast<std::uint32_t>(100 * k + i), 7u});
        }
    }
    const std::vector<float> region = terrain::unreal::blendRegion(texels);
    TEST_CHECK(region.size() == static_cast<std::size_t>(side * side * 4));
    // Texel (i, k) of the tile at row k, column i of the region: the order of TileData.
    bool ordered = true;
    for (int k = 0; k < side; ++k) {
        for (int i = 0; i < side; ++i) {
            const std::size_t at = static_cast<std::size_t>((k * side + i) * 4);
            ordered = ordered && region[at] == static_cast<float>(100 * k + i) &&
                      region[at + 1] == 7.0f && region[at + 2] == 0.0f && region[at + 3] == 1.0f;
        }
    }
    TEST_CHECK(ordered);
    TEST_CHECK(throws<std::invalid_argument>([] { static_cast<void>(terrain::unreal::blendRegion({})); }));
    texels.pop_back();
    TEST_CHECK(throws<std::invalid_argument>(
        [&texels] { static_cast<void>(terrain::unreal::blendRegion(texels)); }));
}

void testPitch() {
    TEST_CHECK(terrain::unreal::blendPitchMeters(0) == 4.0);
    TEST_CHECK(terrain::unreal::blendPitchMeters(1) == 8.0);
    TEST_CHECK(terrain::unreal::blendPitchMeters(2) == 16.0);
    TEST_CHECK(terrain::unreal::blendPitchMeters(3) == 32.0);
    // The last texel lies on the far side of the tile.
    TEST_CHECK((terrain::kTileBlendTexels - 1) * terrain::unreal::blendPitchMeters(2) ==
               terrain::tileSizeMeters(2));
}

void testSlots() {
    BlendAtlas atlas(2);
    TEST_CHECK(atlas.capacity() == 4 && atlas.textureSide() == 2 * terrain::kTileBlendTexels);
    const TileKey a{{0, 0}, 0};
    const TileKey b{{1, 0}, 0};
    const TileKey c{{0, 0}, 1};
    const TileKey d{{4, 4}, 2};
    const TileKey e{{8, 0}, 3};
    TEST_CHECK(atlas.place(a) == 0 && atlas.place(b) == 1 && atlas.place(c) == 2);
    TEST_CHECK(atlas.place(a) == 0); // kept
    TEST_CHECK(atlas.place(d) == 3);
    TEST_CHECK(!atlas.place(e)); // full
    TEST_CHECK(atlas.size() == 4);
    atlas.release(c);
    atlas.release(a);
    atlas.release(a); // nothing more
    TEST_CHECK(atlas.size() == 2 && !atlas.slotOf(a) && atlas.slotOf(b) == 1);
    TEST_CHECK(atlas.place(e) == 0 && atlas.place(a) == 2); // the lowest free slots first
    std::set<int> slots;
    for (const TileKey& key : {a, b, d, e}) {
        slots.insert(atlas.slotOf(key).value_or(-1));
    }
    TEST_CHECK(slots == std::set<int>({0, 1, 2, 3}));
    atlas.clear();
    TEST_CHECK(atlas.size() == 0 && atlas.place(d) == 0);
    // Corners: slots row after row.
    const BlendAtlas big(64);
    constexpr int side = terrain::kTileBlendTexels;
    TEST_CHECK((big.corner(0) == BlendAtlas::Corner{0, 0}));
    TEST_CHECK((big.corner(1) == BlendAtlas::Corner{side, 0}));
    TEST_CHECK((big.corner(64) == BlendAtlas::Corner{0, side}));
    TEST_CHECK((big.corner(64 * 64 - 1) == BlendAtlas::Corner{63 * side, 63 * side}));
    TEST_CHECK(throws<std::out_of_range>([&big] { static_cast<void>(big.corner(64 * 64)); }));
    TEST_CHECK(throws<std::out_of_range>([&big] { static_cast<void>(big.corner(-1)); }));
    TEST_CHECK(throws<std::invalid_argument>([] { BlendAtlas atlas0(0); }));
    TEST_CHECK(throws<std::invalid_argument>([] { BlendAtlas atlas1025(1025); }));
}

} // namespace

int main() {
    testTexelFloats();
    testRegion();
    testPitch();
    testSlots();
    return test::result();
}
