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
// - The light at the foot of the wall (the one line of the agent 33 after the check in Unreal of
//   c96edf7: a regular lace without hue at the foot of the wall of fjord_axe): every texel of the
//   ocean over rock keeps the slopes of its ground, every other texel of the ocean is flat (its
//   normal up), and the normals of a texel of the ocean over rock and of a texel of the land next
//   to it differ by kTurnMedian degrees at most in the median, kTurnHigh at the 90th percentile,
//   on the levels 1 to 3 (69 and 78 degrees when the ocean was flat everywhere: the triangles
//   between them were lit from flat to the wall over one pitch, one sample after the other).
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

#include <algorithm>
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
constexpr double kTurnMedian = 30.0;
constexpr double kTurnHigh = 50.0;

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
    std::vector<std::array<float, 2>> slopes; // of the ground of the samples
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
        win.slopes.push_back({cell.gradX, cell.gradZ});
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

// The angle between the normals of two texels, degrees (the normal of the GPU horizon from the
// slopes of a texel: (-gradX, -gradZ, 1)).
double turnOf(const HorizonTexel& a, const HorizonTexel& b) {
    const auto dot = static_cast<double>(a.gradX * b.gradX + a.gradZ * b.gradZ + 1.0f);
    const auto na = static_cast<double>(std::hypot(a.gradX, a.gradZ, 1.0f));
    const auto nb = static_cast<double>(std::hypot(b.gradX, b.gradZ, 1.0f));
    return std::acos(std::clamp(dot / (na * nb), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
}

// The light at the foot of the walls over the window: the turns of the normals from each texel of
// the ocean over rock to the texels of the land next to it, and the texels of the ocean whose
// slopes are not those of their ground over rock, or not flat elsewhere.
struct Light {
    std::vector<double> turns;
    int wrong = 0;
};

void addLight(const Window& win, Light& light) {
    const auto at = [&win](std::int64_t i, std::int64_t k) {
        return static_cast<std::size_t>(k * win.n + i);
    };
    for (std::int64_t k = 1; k < win.n - 1; ++k) {
        for (std::int64_t i = 1; i < win.n - 1; ++i) {
            const HorizonTexel& t = win.texels[at(i, k)];
            if (!terrain::unreal::isSeaTexel(t)) {
                continue;
            }
            const bool rock = terrain::unreal::isRockSeaTexel(t);
            const std::array<float, 2> slope = rock ? win.slopes[at(i, k)]
                                                    : std::array<float, 2>{0.0f, 0.0f};
            light.wrong += t.gradX == slope[0] && t.gradZ == slope[1] ? 0 : 1;
            for (const auto& [di, dk] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const HorizonTexel& land = win.texels[at(i + di, k + dk)];
                if (rock && !terrain::unreal::isSeaTexel(land)) {
                    light.turns.push_back(turnOf(t, land));
                }
            }
        }
    }
}

double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
}

void testLight() {
    Light light;
    for (int level = 1; level < 4; ++level) {
        addLight(windowOf(1'822'300.0, 9'926'500.0, level), light);
    }
    const double median = quantile(light.turns, 0.5);
    const double high = quantile(light.turns, 0.9);
    std::printf("foot of the wall of fjord_axe, levels 1 to 3: %zu pairs of a texel of the ocean "
                "over rock and one of the land next to it, their normals turning by %.1f degrees "
                "(median), %.1f (90th percentile); %d texels of the ocean with wrong slopes\n",
                light.turns.size(), median, high, light.wrong);
    TEST_CHECK(!light.turns.empty());
    TEST_CHECK(median <= kTurnMedian);
    TEST_CHECK(high <= kTurnHigh);
    TEST_CHECK(light.wrong == 0);
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
    testLight();
    testOpenSea();
    testReef();
    return test::result();
}
