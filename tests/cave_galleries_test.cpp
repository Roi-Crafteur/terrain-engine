// Galleries of the caves (Phase 8C-2): the large connected network of the cave regions, wide
// tunnels along the zero lines of two 2D noises, on one smooth level. Without cave regions, or
// with neutral profiles, there is no gallery, no bank and no sill: their voids leave every other
// void exactly as it is. With them, the network is connected over kilometers and its junctions
// join in 3D; the galleries taper continuously into the rock, are skipped exactly where they
// cannot reach, and are the same at LOD 0 to 2.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <queue>
#include <vector>

namespace {

using caves_test::kSeed;
using caves_test::Rng;
using galleries_test::Component;
using galleries_test::Network;
using terrain::CaveColumn;
using terrain::CaveDistrictParams;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::CaveVoids;

constexpr float kFlatHeight = 300.0f;

CaveParams neutralDistricts() {
    CaveDistrictParams districts;
    districts.profiles.fill(CaveProfile{});
    CaveParams params;
    params.districts = districts;
    return params;
}

// Without cave regions and with neutral ones, at every LOD that carves: no gallery, no bank, no
// sill, at points from the ground down to the deepest caves.
void testNeutralVoids() {
    const terrain::Heightfield heightfield(kSeed);
    const terrain::CaveGround ground = caves_test::heightfieldGround(heightfield);
    int checked = 0;
    bool neutral = true;
    for (const CaveParams& params : {CaveParams{}, neutralDistricts()}) {
        const CaveField field(kSeed, params);
        const float clamp = field.params().voidClamp;
        for (int lod = 0; lod < 3; ++lod) {
            CaveSampler sampler(field, ground, terrain::caveDetailForLod(lod),
                                {-300.0, -400.0, 900.0, -200.0, 1400.0, 1100.0});
            Rng rng(static_cast<std::uint64_t>(lod) + 7);
            for (int n = 0; n < 600; ++n) {
                const double x = rng.range(-300.0, -200.0);
                const double z = rng.range(900.0, 1100.0);
                const float height = ground.height(x, z);
                const double y = static_cast<double>(height) - rng.range(0.0, 110.0);
                const CaveColumn column = sampler.column(x, z, height);
                const CaveVoids v = sampler.voids(column, y);
                neutral = neutral && v.galleries == clamp && v.bank == -clamp && v.sill == -clamp;
                ++checked;
            }
        }
    }
    TEST_CHECK(checked == 3600);
    TEST_CHECK(neutral);
}

// With the neutral gallery, bank and sill, combine is the combination of the caves of 8B: the
// inner components within the limits and the barriers, or the entrances within theirs.
void testCombineIdentity() {
    const CaveField field(kSeed);
    const float clamp = field.params().voidClamp;
    Rng rng(41);
    int same = 0;
    constexpr int kCount = 20000;
    for (int n = 0; n < kCount; ++n) {
        CaveVoids v;
        for (float* value : {&v.spaghetti, &v.noodles, &v.cheese, &v.ravines, &v.entrances,
                             &v.limit, &v.entranceLimit, &v.barrier}) {
            *value = static_cast<float>(rng.range(-clamp, clamp));
        }
        const float inner = std::min({v.spaghetti, v.noodles, v.cheese, v.ravines});
        const float limited = std::max({inner, v.limit, v.barrier});
        const float entrance = std::max({v.entrances, v.entranceLimit, v.barrier});
        const float expected = std::clamp(std::min(limited, entrance), -clamp, clamp);
        CaveVoids clamped = v;
        clamped.galleries = clamp;
        clamped.bank = -clamp;
        clamped.sill = -clamp;
        same += field.combine(v) == expected && field.combine(clamped) == expected ? 1 : 0;
    }
    TEST_CHECK(same == kCount);
}

// The galleries are carved down to LOD 2, as the cheese caverns: the far tiles see the network.
void testLodDetails() {
    for (int lod = 0; lod < 3; ++lod) {
        TEST_CHECK(terrain::caveDetailForLod(lod).has(terrain::kCaveGalleries));
    }
    TEST_CHECK(!terrain::caveDetailForLod(3).has(terrain::kCaveGalleries));
}

// A column of the given gallery field, distance and floor, for the shape of the galleries.
CaveColumn galleryColumn(float presence, float distance, float floor) {
    CaveColumn column;
    column.profile[CaveProfile::kGalleries] = presence;
    column.galleryDistance = distance;
    column.galleryFloor = floor;
    return column;
}

// An ellipse of halfWidth by halfHeight scaled by the field, its floor anchored where the level
// puts it; voidClamp exactly without the field, rock under presenceOn, and continuous in the
// field from 0 to 1.
void testShape() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Tunnels));
    const terrain::GalleryParams& p = field.params().galleries;
    const float clamp = field.params().voidClamp;
    const double floor = 100.0;
    for (const float g : {1.0f, 0.5f}) {
        const float w = p.halfWidth * g;
        const float h = p.halfHeight * g;
        const CaveColumn axis = galleryColumn(g, 0.0f, 100.0f);
        TEST_CHECK(std::abs(field.galleryVoid(axis, floor + h) + h) < 1e-4f);
        TEST_CHECK(field.galleryVoid(axis, floor + 0.1) < 0.0f);
        TEST_CHECK(field.galleryVoid(axis, floor - 0.5) > 0.0f);
        TEST_CHECK(field.galleryVoid(axis, floor + 2.0 * h + 0.5) > 0.0f);
        TEST_CHECK(field.galleryVoid(galleryColumn(g, w - 0.2f, 100.0f), floor + h) < 0.0f);
        TEST_CHECK(field.galleryVoid(galleryColumn(g, w + 0.5f, 100.0f), floor + h) > 0.0f);
    }
    TEST_CHECK(field.galleryVoid(galleryColumn(0.0f, 0.0f, 100.0f), floor + 1.0) == clamp);
    bool rock = true;
    float largestStep = 0.0f;
    float previous = clamp;
    for (int n = 1; n <= 1000; ++n) {
        const float g = static_cast<float>(n) / 1000.0f;
        const float v = field.galleryVoid(galleryColumn(g, 0.0f, 100.0f), floor + 1.0);
        rock = rock && (g >= p.presenceOn || v > 0.0f);
        largestStep = std::max(largestStep, std::abs(v - previous));
        previous = v;
    }
    std::printf("shape: largest void step %.3f m per 0.001 of the field\n", largestStep);
    TEST_CHECK(rock);
    TEST_CHECK(largestStep <= 1.001f * clamp / p.presenceOn / 1000.0f);
}

// The level of the galleries: a convex combination of the 4 nodes around a point, continuous
// with a continuous slope across the lines of the nodes.
void testLevel() {
    const terrain::Heightfield heightfield(kSeed);
    const terrain::CaveGround ground = caves_test::heightfieldGround(heightfield);
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Tunnels));
    terrain::AquiferCache cache(field, ground);
    const double spacing = field.params().galleries.levelSpacing;
    Rng rng(5);
    bool inside = true;
    double largestJump = 0.0;
    double largestKink = 0.0;
    for (int n = 0; n < 4000; ++n) {
        const double x = rng.range(-3000.0, 3000.0);
        const double z = rng.range(-3000.0, 3000.0);
        const int i = static_cast<int>(std::floor(x / spacing));
        const int k = static_cast<int>(std::floor(z / spacing));
        const std::array<double, 4> nodes{cache.galleryNode(i, k), cache.galleryNode(i + 1, k),
                                          cache.galleryNode(i, k + 1),
                                          cache.galleryNode(i + 1, k + 1)};
        const double level = cache.galleryLevel(x, z);
        inside = inside && level >= *std::min_element(nodes.begin(), nodes.end()) - 1e-9 &&
                 level <= *std::max_element(nodes.begin(), nodes.end()) + 1e-9;
        // Across the node line x = i spacing: the value and the slope on both sides.
        const double line = i * spacing;
        constexpr double kEps = 1e-4;
        const double left = cache.galleryLevel(line - kEps, z);
        const double right = cache.galleryLevel(line + kEps, z);
        largestJump = std::max(largestJump, std::abs(right - left));
        const double slopeLeft = (left - cache.galleryLevel(line - 2.0 * kEps, z)) / kEps;
        const double slopeRight = (cache.galleryLevel(line + 2.0 * kEps, z) - right) / kEps;
        largestKink = std::max(largestKink, std::abs(slopeRight - slopeLeft));
    }
    std::printf("level: jump %.2g m, kink %.2g across the node lines\n", largestJump,
                largestKink);
    TEST_CHECK(inside);
    TEST_CHECK(largestJump < 1e-3);
    TEST_CHECK(largestKink < 1e-2);
}

// The density of the samplers is exactly the carve of the full void wherever they skip the
// noises (the galleries enter the bound of the skip); the galleries are the same at LOD 0 and
// LOD 2, and the same from any sampler, any box, and without a cache.
void testExactAndStable() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Tunnels));
    const terrain::Heightfield heightfield(kSeed);
    const terrain::CaveGround relief = caves_test::heightfieldGround(heightfield);
    const terrain::CaveGround flat = caves_test::flatGround(kFlatHeight);
    int exact = 0;
    int samples = 0;
    int galleryAir = 0;
    for (const terrain::CaveGround* ground : {&flat, &relief}) {
        for (int lod = 0; lod < 3; ++lod) {
            const terrain::CaveDetail detail = terrain::caveDetailForLod(lod);
            CaveSampler sampler(field, *ground, detail,
                                {-500.0, -400.0, -1500.0, -404.0, 1600.0, -1404.0});
            for (int k = 0; k <= 96; k += 3) {
                for (int i = 0; i <= 96; i += 3) {
                    const double x = -500.0 + i;
                    const double z = -1500.0 + k;
                    const float height = ground->height(x, z);
                    const CaveColumn column = sampler.column(x, z, height);
                    for (int depth = 10; depth <= 40; ++depth) {
                        const double y = static_cast<double>(height) - depth;
                        const float t = caves_test::terrainDensity(y, height);
                        const float carved = sampler.density(column, y, t);
                        exact +=
                            carved == field.carve(t, sampler.voidAt(column, y), detail) ? 1 : 0;
                        galleryAir += sampler.voids(column, y).galleries < 0.0f ? 1 : 0;
                        ++samples;
                    }
                }
            }
        }
    }
    std::printf("skips: %d of %d samples exact, %d in a gallery\n", exact, samples, galleryAir);
    TEST_CHECK(exact == samples);
    TEST_CHECK(galleryAir > 1000);

    CaveSampler lod0(field, relief, terrain::caveDetailForLod(0),
                     {0.0, -400.0, 0.0, 600.0, 1600.0, 600.0});
    CaveSampler lod2(field, relief, terrain::caveDetailForLod(2),
                     {-300.0, -400.0, -300.0, 900.0, 1600.0, 900.0});
    terrain::AquiferCache other(field, relief);
    Rng rng(9);
    bool same = true;
    for (int n = 0; n < 3000; ++n) {
        const double x = rng.range(0.0, 600.0);
        const double z = rng.range(0.0, 600.0);
        const float height = relief.height(x, z);
        const double y = static_cast<double>(height) - rng.range(10.0, 60.0);
        const CaveColumn a = lod0.column(x, z, height);
        const CaveColumn b = lod2.column(x, z, height);
        const CaveColumn alone = field.column(x, z, height, relief, {});
        const CaveColumn cached = field.column(x, z, height, relief, {}, &other, false);
        same = same && a.galleryDistance == b.galleryDistance &&
               a.galleryFloor == b.galleryFloor && alone.galleryFloor == a.galleryFloor &&
               alone.galleryDistance == a.galleryDistance &&
               cached.galleryFloor == a.galleryFloor &&
               lod0.voids(a, y).galleries == lod2.voids(b, y).galleries;
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    testNeutralVoids();
    testCombineIdentity();
    testLodDetails();
    testShape();
    testLevel();
    testExactAndStable();
    return test::result();
}
