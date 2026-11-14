// The sea near a pixel as the materials of Unreal read it (agent 52: the land below the sea level is
// not the sea; Scripts/temporary_terrain_hlsl.py: SEA_NEAR_HLSL, replayed here by seaNear, and the
// wet rock of SHADE_BOTTOM, max(Sea, smoothstep(-1, 0, Z)), by wetGate) on the real texels of level
// 0 of the GPU horizon (Bridge/horizon_textures: isSeaTexel), the default world with its planet file,
// as Unreal loads it:
// - on the coasts of agent 33 (its places cote_* and stack_* of data/dev/places.json): every point
//   of the ground below the top of the wet band (one and a half WetBandM: 4.8 m) that may be rock
//   (a slope of 28 degrees or more, or the rock of the core) keeps its wet rock exactly (the gate
//   1): near the sea (kSeaNearM, temporary_terrain_look.py: SEA_NEAR_M) or above 0 m; how far from
//   the sea each lies, measured, and the low ground (below 1 m) beyond the ring, where the depth
//   offset of the horizon under the tiles grows (no surface behind it: no change seen);
// - the dry depression n. 27 (depression_seche_haut and depression_seche_fond): no texel of the
//   ocean near its floor, whose texels are of the land below the sea level (the altitude is never
//   read); none of its rock from -1 m down wet (its floor and the foot of its walls); the share of
//   its rock left wet by the altitude between -1 and 4.8 m on its rims, over the depression and
//   around each place, measured;
// - on the open sea, Sea = 1.
#include "coast_planet_support.hpp"
#include "horizon_textures.hpp"

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

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
// SEA_NEAR_M of temporary_terrain_look.py (lows_shader_scripts_test.py checks they are equal) and
// SEA_RING_MAX of temporary_terrain_hlsl.py.
constexpr double kSeaNearM = 16.0;
constexpr int kRingMax = 2;
// The top of the wet band (1.5 WetBandM + 0.3 m) and the slope from which the ground may be rock
// (RockStartDeg 34 less the jitter of the shading, 5.5 degrees).
constexpr float kWetTop = 4.8f;
constexpr double kRockSlope = 28.0;
// The ground measured around a place (the views look 200 m to 1.8 km away from it), every step.
constexpr std::int64_t kMaxHalf = 300; // samples of level 0 on either side of a place, at most

struct World {
    const terrain::Heightfield& field = coast_planet::field();
    terrain::Ecology ecology{kSeed};
    terrain::HorizonParams params{};
    terrain::HorizonSampler sampler{field, ecology, params};
};

const World& world() {
    static const World w;
    return w;
}

// The texels of level 0 around a place: whether each is of the ocean, row by row.
struct Window {
    int pitch = 0;
    HorizonRect rect{};
    std::int64_t n = 0;
    std::vector<std::uint8_t> sea;
    std::vector<float> heights;

    [[nodiscard]] bool seaAt(std::int64_t i, std::int64_t k) const {
        i = std::clamp<std::int64_t>(i, 0, n - 1);
        k = std::clamp<std::int64_t>(k, 0, n - 1);
        return sea[static_cast<std::size_t>(k * n + i)] != 0;
    }
};

// The texels of level 0 within around meters of (x, z), and a margin.
Window windowOf(double x, double z, double around) {
    const World& w = world();
    const terrain::HorizonIndex c = terrain::canonicalCenter(w.params, 0, x, z);
    Window win;
    win.pitch = terrain::levelPitch(w.params, 0);
    const std::int64_t half =
        std::min(kMaxHalf, static_cast<std::int64_t>(around / win.pitch) + 8);
    win.rect = {c.x - half, c.z - half, c.x + half, c.z + half};
    win.n = 2 * half;
    for (const HorizonCell& cell : w.sampler.sample(0, win.rect)) {
        win.sea.push_back(terrain::unreal::isSeaTexel(terrain::unreal::horizonTexel(cell, true)) ? 1
                                                                                                 : 0);
        win.heights.push_back(cell.height);
    }
    return win;
}

// The coordinates of (x, z) in texels of the window (the image nearest to its first sample).
std::array<double, 2> texelOf(const Window& win, double x, double z) {
    const terrain::WorldSize& size = world().field.world();
    const double x0 = terrain::samplePosition(win.rect.x0, win.pitch);
    const double z0 = terrain::samplePosition(win.rect.z0, win.pitch);
    return {(terrain::nearestImage(x, x0, size.x) - x0) / win.pitch,
            (terrain::nearestImage(z, z0, size.z) - z0) / win.pitch};
}

// SEA_NEAR_HLSL: 1 where each of the 4 texels around (u, v) has a texel of the ocean within ring
// texels of it (both ways along each axis), 0 where none has, bilinear in between.
double seaNear(const Window& win, double u, double v, int ring) {
    const double qu = std::clamp(u, 0.0, static_cast<double>(win.n - 1) - 0.001);
    const double qv = std::clamp(v, 0.0, static_cast<double>(win.n - 1) - 0.001);
    const auto i0 = static_cast<std::int64_t>(std::floor(qu));
    const auto k0 = static_cast<std::int64_t>(std::floor(qv));
    std::array<double, 4> corner{};
    for (int dj = -ring; dj <= ring + 1; ++dj) {
        for (int di = -ring; di <= ring + 1; ++di) {
            if (win.seaAt(i0 + di, k0 + dj)) {
                corner[0] = std::max(corner[0], di <= ring && dj <= ring ? 1.0 : 0.0);
                corner[1] = std::max(corner[1], di >= 1 - ring && dj <= ring ? 1.0 : 0.0);
                corner[2] = std::max(corner[2], di <= ring && dj >= 1 - ring ? 1.0 : 0.0);
                corner[3] = std::max(corner[3], di >= 1 - ring && dj >= 1 - ring ? 1.0 : 0.0);
            }
        }
    }
    const double fu = qu - static_cast<double>(i0);
    const double fv = qv - static_cast<double>(k0);
    const double a = corner[0] + (corner[1] - corner[0]) * fu;
    const double b = corner[2] + (corner[3] - corner[2]) * fu;
    return a + (b - a) * fv;
}

// The ring a point needs to read Sea = 1: the largest, over the 4 texels around it, of the
// distance (texels, Chebyshev) to the nearest texel of the ocean; 16 where none that near.
int ringNeeded(const Window& win, double u, double v) {
    const auto i0 = static_cast<std::int64_t>(std::floor(u));
    const auto k0 = static_cast<std::int64_t>(std::floor(v));
    int worst = 0;
    for (int c = 0; c < 4; ++c) {
        const std::int64_t ci = i0 + (c & 1);
        const std::int64_t ck = k0 + (c >> 1);
        int best = 16;
        for (int r = 0; r < best; ++r) {
            for (int dk = -r; dk <= r && best > r; ++dk) {
                for (int di = -r; di <= r; ++di) {
                    if ((std::abs(di) == r || std::abs(dk) == r) && win.seaAt(ci + di, ck + dk)) {
                        best = r;
                        break;
                    }
                }
            }
        }
        worst = std::max(worst, best);
    }
    return worst;
}

// The wet rock of SHADE_BOTTOM below the sea level: max(Sea, smoothstep(-1, 0, Z)).
double wetGate(double sea, double z) {
    const double t = std::clamp(z + 1.0, 0.0, 1.0);
    return std::max(sea, t * t * (3.0 - 2.0 * t));
}

int ringOf(double meters, int pitch) {
    return std::clamp(static_cast<int>(std::ceil(meters / pitch)), 1, kRingMax);
}

struct Place {
    const char* name;
    double x;
    double z;
    double around = 1000.0; // meters measured on either side
    double step = 8.0;
};

// The coasts of agent 33 (data/dev/places.json): the cold coast seen from 1.8 km.
constexpr std::array<Place, 10> kCoasts = {{
    {"cote_froide", 3624471.0, -10006796.0, 2000.0, 16.0},
    {"cote_falaise_800m", 429427.2, -855233.8},
    {"cote_falaise_encoche", 429778.8, -855913.3},
    {"cote_calanque_300m", 23416410.3, -2119673.8},
    {"cote_stack", -15901198.7, -6233826.8},
    {"cote_fjord_axe", 1819677.0, 9928285.0},
    {"stack_aiguille", -15901439.4, -6233918.6},
    {"stack_pilier", -26464363.8, 7232094.7},
    {"stack_fendu", 23228261.2, -2040630.1},
    {"stack_arche", -26488769.4, 7401625.9},
}};

// The low points of a row of the ground: below the top of the wet band, not the sea.
struct Low {
    double x;
    double z;
    float height;
    bool rock;
};
std::vector<Low> lowPoints(const Place& place) {
    const terrain::Heightfield& field = world().field;
    std::vector<Low> out;
    std::vector<double> xs;
    for (double dx = -place.around; dx <= place.around; dx += place.step) {
        xs.push_back(place.x + dx);
    }
    std::vector<terrain::HeightSample> row(xs.size());
    for (double dz = -place.around; dz <= place.around; dz += place.step) {
        const std::vector<double> zs(xs.size(), place.z + dz);
        field.samplesAt(xs, zs, row);
        for (std::size_t k = 0; k < xs.size(); ++k) {
            const terrain::HeightSample& s = row[k];
            if (s.water == terrain::WaterKind::Ocean || !(s.height < kWetTop)) {
                continue;
            }
            const double x = xs[k];
            const double z = zs[k];
            const double gx = (field.heightAt(x + 1.0, z) - field.heightAt(x - 1.0, z)) / 2.0;
            const double gz = (field.heightAt(x, z + 1.0) - field.heightAt(x, z - 1.0)) / 2.0;
            const double slope = std::atan(std::hypot(gx, gz)) * 180.0 / 3.14159265358979;
            out.push_back({x, z, s.height, slope >= kRockSlope || s.rock > 0.0f});
        }
    }
    return out;
}

void testCoasts() {
    std::size_t rockPoints = 0;
    std::size_t kept = 0;
    std::size_t deeper = 0;
    std::size_t lowCount = 0;
    for (const Place& place : kCoasts) {
        const Window win = windowOf(place.x, place.z, place.around);
        const int ring = ringOf(kSeaNearM, win.pitch);
        int placeWorst = 0;
        std::size_t placeRock = 0;
        std::size_t placeAbove = 0;
        for (const Low& p : lowPoints(place)) {
            const auto [u, v] = texelOf(win, p.x, p.z);
            const double sea = seaNear(win, u, v, ring);
            ++lowCount;
            // The depth offset: lerp(1, saturate(Ground), Sea), Ground at least 0.01 on the land.
            deeper += p.height < 0.99f && sea < 1.0 ? 1 : 0;
            if (p.rock) {
                ++placeRock;
                placeWorst = std::max(placeWorst, ringNeeded(win, u, v));
                placeAbove += sea < 1.0 ? 1 : 0;
                TEST_CHECK(wetGate(sea, static_cast<double>(p.height)) == 1.0);
                kept += wetGate(sea, static_cast<double>(p.height)) == 1.0 ? 1 : 0;
            }
        }
        rockPoints += placeRock;
        std::printf("%-22s %6zu points of rock below %.1f m, the farthest %2d texels of %d m from "
                    "the sea, %5zu beyond %.0f m kept by their altitude\n",
                    place.name, placeRock, static_cast<double>(kWetTop), placeWorst, win.pitch,
                    placeAbove, kSeaNearM);
    }
    std::printf("the 10 coasts: %zu of %zu points of rock below %.1f m as wet as before; of %zu "
                "points of low ground, %zu below 1 m beyond %.0f m (the depth offset grows there)\n",
                kept, rockPoints, static_cast<double>(kWetTop), lowCount, deeper, kSeaNearM);
    TEST_CHECK(rockPoints > 1000 && kept == rockPoints);
}

// The texels of level 0 around the floor of the depression n. 27: none of the ocean, all of the
// land below the sea level.
void testDryFloor() {
    constexpr Place kFloor{"depression_seche_fond", -410000.0, 4589844.0, 300.0, 25.0};
    const Window win = windowOf(kFloor.x, kFloor.z, 300.0);
    const int ring = ringOf(kSeaNearM, win.pitch);
    double most = 0.0;
    float highest = -1e9f;
    for (double dz = -300.0; dz <= 300.0; dz += 25.0) {
        for (double dx = -300.0; dx <= 300.0; dx += 25.0) {
            const auto [u, v] = texelOf(win, kFloor.x + dx, kFloor.z + dz);
            most = std::max(most, seaNear(win, u, v, ring));
        }
    }
    std::size_t seaTexels = 0;
    for (std::size_t k = 0; k < win.sea.size(); ++k) {
        seaTexels += win.sea[k];
        highest = std::max(highest, win.heights[k]);
    }
    std::printf("dry depression n. 27: Sea at most %.2f, %zu texels of the ocean, ground %.1f m "
                "at its point, %.1f m at most around\n",
                most, seaTexels, static_cast<double>(world().field.heightAt(kFloor.x, kFloor.z)),
                static_cast<double>(highest));
    TEST_CHECK(most == 0.0 && seaTexels == 0);
    TEST_CHECK(world().field.heightAt(kFloor.x, kFloor.z) < -150.0f && highest < 0.0f);
}

// The rock of the depression n. 27, every kStepM over 1.3 of its radius: far from the sea (Sea 0),
// none of it wet from -1 m down; the share left wet by the altitude on its rims, over the
// depression and within kViews of each place.
void testDryRims() {
    constexpr double kStepM = 400.0;
    constexpr std::array<Place, 2> kViews = {{
        {"depression_seche_haut", -380000.0, 4589844.0, 20'000.0},
        {"depression_seche_fond", -410000.0, 4589844.0, 5'000.0},
    }};
    const terrain::Heightfield& field = world().field;
    terrain::Depression d{};
    double best = 1e30;
    for (const terrain::Depression& e : field.depressions()) {
        const double far = std::hypot(e.x - kViews[1].x, e.z - kViews[1].z);
        if (far < best) {
            best = far;
            d = e;
        }
    }
    std::array<std::size_t, 3> rock{};
    std::array<std::size_t, 3> band{};
    std::size_t floorRock = 0;
    std::size_t nearSea = 0;
    std::vector<double> xs;
    for (double dx = -1.3 * d.radius; dx <= 1.3 * d.radius; dx += kStepM) {
        xs.push_back(d.x + dx);
    }
    // The row and the four around each point a meter away (the slope), in batches.
    std::array<std::vector<double>, 3> rowX;
    for (int k = 0; k < 3; ++k) {
        for (const double x : xs) {
            rowX[static_cast<std::size_t>(k)].push_back(x + (k - 1));
        }
    }
    std::array<std::vector<terrain::HeightSample>, 5> rows;
    for (std::vector<terrain::HeightSample>& r : rows) {
        r.resize(xs.size());
    }
    for (double dz = -1.3 * d.radius; dz <= 1.3 * d.radius; dz += kStepM) {
        const std::vector<double> zs(xs.size(), d.z + dz);
        const std::vector<double> below(xs.size(), d.z + dz - 1.0);
        const std::vector<double> above(xs.size(), d.z + dz + 1.0);
        field.samplesAt(xs, zs, rows[0]);
        field.samplesAt(rowX[0], zs, rows[1]);
        field.samplesAt(rowX[2], zs, rows[2]);
        field.samplesAt(xs, below, rows[3]);
        field.samplesAt(xs, above, rows[4]);
        for (std::size_t k = 0; k < xs.size(); ++k) {
            const terrain::HeightSample& s = rows[0][k];
            // Every texel of the ocean lies within kSeaNearM and a texel of a point near the sea.
            nearSea += static_cast<double>(s.continentalness) * 1.5e6 < 1000.0 ? 1 : 0;
            const double x = xs[k];
            const double z = zs[k];
            const double gx = static_cast<double>(rows[2][k].height - rows[1][k].height) / 2.0;
            const double gz = static_cast<double>(rows[4][k].height - rows[3][k].height) / 2.0;
            const double slope = std::atan(std::hypot(gx, gz)) * 180.0 / 3.14159265358979;
            if (!(slope >= kRockSlope || s.rock > 0.0f)) {
                continue;
            }
            const double wet = wetGate(0.0, static_cast<double>(s.height));
            const bool inBand = s.height > -1.0f && s.height < kWetTop;
            floorRock += s.height <= -1.0f ? 1 : 0;
            TEST_CHECK(s.height > -1.0f || wet == 0.0);
            for (std::size_t v = 0; v < 3; ++v) {
                const bool in =
                    v == 0 || std::hypot(x - kViews[v - 1].x, z - kViews[v - 1].z) <
                                  kViews[v - 1].around;
                rock[v] += in ? 1 : 0;
                band[v] += in && inBand ? 1 : 0;
            }
        }
    }
    std::printf("dry depression n. 27 (radius %.0f m, floor %.0f m): %zu points near the sea; "
                "%zu points of rock from -1 m down, none wet\n",
                d.radius, static_cast<double>(d.floor), nearSea, floorRock);
    const std::array<const char*, 3> names = {"the whole depression", kViews[0].name,
                                              kViews[1].name};
    for (std::size_t v = 0; v < 3; ++v) {
        const double share = 100.0 * static_cast<double>(band[v]) /
                             static_cast<double>(std::max<std::size_t>(rock[v], 1));
        std::printf("  %-24s rock still wet between -1 and %.1f m: %zu of %zu points (%.2f %%)\n",
                    names[v], static_cast<double>(kWetTop), band[v], rock[v], share);
    }
    TEST_CHECK(nearSea == 0 && floorRock > 20);
}

void testOpenSea() {
    const Window win = windowOf(1234.5, -6789.25, 100.0);
    TEST_CHECK(world().field.sampleAt(1234.5, -6789.25).water == terrain::WaterKind::Ocean);
    const auto [u, v] = texelOf(win, 1234.5, -6789.25);
    TEST_CHECK(seaNear(win, u, v, 1) == 1.0);
}

} // namespace

int main() {
    testCoasts();
    testDryFloor();
    testDryRims();
    testOpenSea();
    return test::result();
}
