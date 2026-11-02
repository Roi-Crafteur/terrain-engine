// The materials of the coasts as the GPU horizon of Unreal draws them (agent 33, after the check in
// Unreal of the commit 9afaa51): beyond the tiles (512 m) the horizon draws the ground, its texels
// (Bridge/horizon_textures) holding on the ocean the ground under the sea instead of a material,
// which the material draws as the sand of the shallows and carries up the foot of the walls
// between its samples (the comb of pale points along the wall of a fjord seen from 3 km).
// - Along the steep wall of a fjord, on the levels 0 to 3 of the horizon: every texel of the ocean
//   whose ground the samples one or two pitches away rise above by a pitch per pitch at least is
//   a texel of the ocean over bare rock (isRockSeaTexel: the material draws rock, not sand), and
//   every texel of the ocean gives back its ground (texelGround: exactly, within kRockPrecision
//   over rock).
// - The blends of the GPU horizon (agent 29: the colours of the GPU horizon come from them, the
//   packed surface only where a texel holds none): the blend of each of those texels of the ocean
//   over rock, as the GPU decodes it from its channels (horizon_blend_texels), is all rock
//   (materials, levels 0 to 3: the point blends); in the open sea no blend weighs rock.
// - Out in the open sea none is.
// - The samples of the rocks of a reef above the water are texels of the land of rock, and so
//   are their blends.
#include "horizon_blend_texels.hpp"
#include "horizon_mesh.hpp"
#include "horizon_textures.hpp"
#include "terrain_surface.hpp"

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::HorizonCell;
using terrain::HorizonRect;
using terrain::SurfaceMaterial;
using terrain::unreal::HorizonTexel;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr std::int64_t kHalf = 24; // samples on either side of the place
// The ground of a texel of the ocean over rock is given back within the precision of a float
// near kSeaRockCode (1/128 m); the others exactly.
constexpr float kRockPrecision = 0.01f;

struct World {
    terrain::Heightfield field{kSeed};
    terrain::Ecology ecology{kSeed};
    terrain::HorizonParams params{};
    terrain::HorizonSampler sampler{field, ecology, params};
};

const World& world() {
    static const World w;
    return w;
}

// The texels of a level around (x, z), row by row, and the heights of their samples.
struct Window {
    int pitch = 0;
    std::int64_t n = 0;
    std::vector<HorizonTexel> texels;
    std::vector<float> heights;
    std::vector<terrain::BlendTexel> blends;
};

// The share of the weight of the materials of a blend on rock, as the GPU horizon reads it: its
// channels decoded (blendOfChannels), -1 where they hold no blend.
float rockShare(terrain::BlendTexel texel) {
    const auto read = terrain::unreal::blendOfChannels(terrain::unreal::blendChannels(texel));
    if (!read) {
        return -1.0f;
    }
    const terrain::SurfaceBlend blend = terrain::unpackBlend(*read);
    float rock = 0.0f;
    for (std::size_t s = 0; s < blend.materials.size(); ++s) {
        rock += blend.materials[s] == SurfaceMaterial::Rock ? blend.materialWeights[s] : 0.0f;
    }
    return rock;
}

Window windowOf(double x, double z, int level) {
    const World& w = world();
    const terrain::HorizonIndex c = terrain::canonicalCenter(w.params, level, x, z);
    const HorizonRect rect{c.x - kHalf, c.z - kHalf, c.x + kHalf, c.z + kHalf};
    Window win;
    win.pitch = terrain::levelPitch(w.params, level);
    win.n = 2 * kHalf;
    for (const HorizonCell& cell : w.sampler.sample(level, rect)) {
        win.texels.push_back(terrain::unreal::horizonTexel(cell, true));
        win.heights.push_back(cell.height);
        win.blends.push_back(cell.blend);
    }
    return win;
}

// The texels of the ocean at the foot of a wall drawn as sand, the texels of the ocean whose
// ground is not given back, the texels of the ocean over rock, over the window.
struct Count {
    int sea = 0;
    int walls = 0; // at the foot of a wall: all drawn as sand before (no rock code)
    int combs = 0;
    int lost = 0;
    int rock = 0;
    int soft = 0;  // of the ocean over rock, whose blend is not all rock
    int veined = 0; // of the ocean not over rock, whose blend weighs rock
};

Count countOf(const Window& win) {
    Count c;
    const auto at = [&win](std::int64_t i, std::int64_t k) {
        return static_cast<std::size_t>(k * win.n + i);
    };
    for (std::int64_t k = 2; k < win.n - 2; ++k) {
        for (std::int64_t i = 2; i < win.n - 2; ++i) {
            const HorizonTexel& t = win.texels[at(i, k)];
            if (!terrain::unreal::isSeaTexel(t)) {
                continue;
            }
            ++c.sea;
            const float ground = win.heights[at(i, k)];
            const float back = terrain::unreal::texelGround(t);
            const bool rock = terrain::unreal::isRockSeaTexel(t);
            c.lost += (rock ? std::abs(back - ground) <= kRockPrecision : back == ground) ? 0 : 1;
            c.rock += rock ? 1 : 0;
            const float share = rockShare(win.blends[at(i, k)]);
            c.soft += rock && share < 1.0f ? 1 : 0;
            c.veined += !rock && share != 0.0f ? 1 : 0;
            bool wall = false;
            for (const auto& [di, dk] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                for (const int steps : {1, 2}) {
                    const float h = win.heights[at(i + di * steps, k + dk * steps)];
                    wall = wall || h - ground >= static_cast<float>(steps * win.pitch);
                }
            }
            c.walls += wall ? 1 : 0;
            c.combs += wall && !rock ? 1 : 0;
        }
    }
    return c;
}

struct Place {
    const char* name;
    double x;
    double z;
};

void testWalls() {
    // The foot of the steep wall of the fjord of fjord_axe, and its mouth.
    constexpr std::array<Place, 2> kWalls{{{"fjord wall", 1'822'300.0, 9'926'500.0},
                                           {"fjord mouth", 1'822'496.0, 9'927'259.0}}};
    Count all;
    for (const Place& p : kWalls) {
        for (int level = 0; level < 4; ++level) {
            const Count c = countOf(windowOf(p.x, p.z, level));
            std::printf("%s, level %d: %d texels of the ocean, %d over rock, %d at the foot of a "
                        "wall, %d of them drawn as sand, %d grounds not given back; blends: %d "
                        "over rock not all rock, %d elsewhere weighing rock\n",
                        p.name, level, c.sea, c.rock, c.walls, c.combs, c.lost, c.soft, c.veined);
            all.combs += c.combs;
            all.lost += c.lost;
            all.rock += c.rock;
            all.soft += c.soft;
            all.veined += c.veined;
        }
    }
    TEST_CHECK(all.rock > 0);
    TEST_CHECK(all.combs == 0);
    TEST_CHECK(all.lost == 0);
    TEST_CHECK(all.soft == 0);
    TEST_CHECK(all.veined == 0);
}

void testOpenSea() {
    const Count c = countOf(windowOf(2'000'000.0, 2'000'000.0, 2));
    std::printf("open sea, level 2: %d texels of the ocean, %d over rock, %d grounds not given "
                "back, %d blends weighing rock\n",
                c.sea, c.rock, c.lost, c.veined);
    TEST_CHECK(c.sea > 0);
    TEST_CHECK(c.rock == 0);
    TEST_CHECK(c.lost == 0);
    TEST_CHECK(c.veined == 0);
}

// The rocks of a reef off the cove group of the stop point 2 bis, above the water: rock, their
// blends too.
void testReef() {
    int dry = 0;
    int rock = 0;
    int blends = 0;
    const World& w = world();
    for (const terrain::CoastStack& r :
         w.field.stacksIn(23'344'200.0, -2'077'400.0, 23'344'900.0, -2'076'700.0)) {
        const terrain::HorizonIndex c = terrain::canonicalCenter(w.params, 0, r.x, r.z);
        const HorizonRect rect{c.x - 4, c.z - 4, c.x + 4, c.z + 4};
        for (const HorizonCell& cell : w.sampler.sample(0, rect)) {
            const HorizonTexel t = terrain::unreal::horizonTexel(cell, true);
            if (terrain::unreal::isSeaTexel(t) || cell.height <= 0.0f) {
                continue;
            }
            ++dry;
            rock += terrain::unreal::unpackSurface(t.color).material ==
                            terrain::SurfaceMaterial::Rock
                        ? 1
                        : 0;
            blends += rockShare(cell.blend) == 1.0f ? 1 : 0;
        }
    }
    std::printf("reef: %d texels of land around its rocks, %d of rock, %d of them blends all "
                "rock\n",
                dry, rock, blends);
    TEST_CHECK(dry > 0);
    TEST_CHECK(rock == dry);
    TEST_CHECK(blends == dry);
}

} // namespace

int main() {
    testWalls();
    testOpenSea();
    testReef();
    return test::result();
}
