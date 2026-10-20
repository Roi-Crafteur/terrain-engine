// Galleries of the caves (Phase 8C-2): the large connected network of the cave regions. Without
// cave regions, or with neutral profiles, there is no gallery, no bank and no sill: their voids
// leave every other void exactly as it is.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/cave_tiles.hpp"

#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdio>

namespace {

using caves_test::kSeed;
using caves_test::Rng;
using terrain::CaveColumn;
using terrain::CaveDistrictParams;
using terrain::CaveField;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::CaveVoids;

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

} // namespace

int main() {
    testNeutralVoids();
    testCombineIdentity();
    testLodDetails();
    return test::result();
}
