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
// - Out in the open sea none is.
// - The samples of the rocks of a reef above the water are texels of the land of rock.
#include "horizon_mesh.hpp"
#include "horizon_textures.hpp"
#include "terrain_surface.hpp"

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
};

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
            c.rock += terrain::unreal::isRockSeaTexel(t) ? 1 : 0;
            bool wall = false;
            for (const auto& [di, dk] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                for (const int steps : {1, 2}) {
                    const float h = win.heights[at(i + di * steps, k + dk * steps)];
                    wall = wall || h - ground >= static_cast<float>(steps * win.pitch);
                }
            }
            c.walls += wall ? 1 : 0;
            c.combs += wall && !terrain::unreal::isRockSeaTexel(t) ? 1 : 0;
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
    int combs = 0;
    int lost = 0;
    int rock = 0;
    for (const Place& p : kWalls) {
        for (int level = 0; level < 4; ++level) {
            const Count c = countOf(windowOf(p.x, p.z, level));
            std::printf("%s, level %d: %d texels of the ocean, %d over rock, %d at the foot of a "
                        "wall, %d of them drawn as sand, %d grounds not given back\n",
                        p.name, level, c.sea, c.rock, c.walls, c.combs, c.lost);
            combs += c.combs;
            lost += c.lost;
            rock += c.rock;
        }
    }
    TEST_CHECK(rock > 0);
    TEST_CHECK(combs == 0);
    TEST_CHECK(lost == 0);
}

void testOpenSea() {
    const Count c = countOf(windowOf(2'000'000.0, 2'000'000.0, 2));
    std::printf("open sea, level 2: %d texels of the ocean, %d over rock, %d grounds not given "
                "back\n",
                c.sea, c.rock, c.lost);
    TEST_CHECK(c.sea > 0);
    TEST_CHECK(c.rock == 0);
    TEST_CHECK(c.lost == 0);
}

// The rocks of a reef off the cove group of the stop point 2 bis, above the water: rock.
void testReef() {
    int dry = 0;
    int rock = 0;
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
        }
    }
    std::printf("reef: %d texels of land around its rocks, %d of rock\n", dry, rock);
    TEST_CHECK(dry > 0);
    TEST_CHECK(rock == dry);
}

} // namespace

int main() {
    testWalls();
    testOpenSea();
    testReef();
    return test::result();
}
