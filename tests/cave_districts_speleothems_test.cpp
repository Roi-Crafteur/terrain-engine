// Speleothems of the cave districts (Phase 8C-1): the dripstone districts hold many more and
// longer ones, the lush ones a few more, the glacier ones the same as the neutral caves but of
// ice, and the district of a point never depends on the area being placed. Each profile is
// measured as the profile of every district (uniformCaves) against the neutral one, over dry
// aquifers so that only the profile of the speleothems and of the shapes of the caves differs.
#include "terrain/generation/speleothems.hpp"

#include "cave_districts_carving_support.hpp"
#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "speleothems_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using caves_test::environmentOf;
using districts_test::defaultProfile;
using districts_test::dry;
using districts_test::uniformCaves;
using speleothems_test::countIce;
using speleothems_test::placeWith;
using speleothems_test::Square;
using terrain::CaveDistrictType;
using terrain::CaveProfile;
using terrain::Speleothem;

// Squares far apart, over flat ground of a mild climate.
const Square kSquares[] = {{0.0, 0.0, 64.0}, {2000.0, -1500.0, 64.0}, {-3000.0, 800.0, 64.0}};

std::vector<Speleothem> placeOver(const CaveProfile& profile) {
    const terrain::CaveEnvironment mild = environmentOf(terrain::Biome::Grassland, 12.0f, 0.5f);
    std::vector<Speleothem> all;
    for (const Square& square : kSquares) {
        const std::vector<Speleothem> part = placeWith(dry(uniformCaves(profile)), mild, square);
        all.insert(all.end(), part.begin(), part.end());
    }
    return all;
}

double meanLength(const std::vector<Speleothem>& list) {
    double total = 0.0;
    for (const Speleothem& s : list) {
        total += static_cast<double>(s.length);
    }
    return list.empty() ? 0.0 : total / static_cast<double>(list.size());
}

float longest(const std::vector<Speleothem>& list) {
    float result = 0.0f;
    for (const Speleothem& s : list) {
        result = std::max(result, s.length);
    }
    return result;
}

// Dripstone: three times the chance and one and a half times the length of the neutral
// speleothems, so at least twice as many, longer ones, some beyond the neutral limit.
void testDripstone(const std::vector<Speleothem>& neutral) {
    const std::vector<Speleothem> dripstone =
        placeOver(defaultProfile(CaveDistrictType::Dripstone));
    const float limit = terrain::SpeleothemParams{}.maxLength;
    std::printf("dripstone: %zu speleothems against %zu neutral, mean length %.2f against %.2f m, "
                "longest %.2f against %.2f m\n",
                dripstone.size(), neutral.size(), meanLength(dripstone), meanLength(neutral),
                static_cast<double>(longest(dripstone)), static_cast<double>(longest(neutral)));
    TEST_CHECK(neutral.size() >= 40);
    TEST_CHECK(dripstone.size() >= 2 * neutral.size());
    TEST_CHECK(meanLength(dripstone) > 1.1 * meanLength(neutral));
    TEST_CHECK(longest(neutral) <= limit && longest(dripstone) > limit);
}

// Glacier: the caves and the chance are the neutral ones, so the same speleothems, all of ice,
// where the neutral ones (a mild climate, dry cells) are not. Lush: 1.2 times the chance over the
// same caves, so the neutral speleothems and a few more.
void testGlacierAndLush(const std::vector<Speleothem>& neutral) {
    const std::vector<Speleothem> glacier = placeOver(defaultProfile(CaveDistrictType::Glacier));
    // Without the wet offset of the lush districts: the water of the wet cells it creates would
    // keep some speleothems from standing under its level.
    CaveProfile lushProfile = defaultProfile(CaveDistrictType::Lush);
    lushProfile[CaveProfile::kWetOffset] = 0.0f;
    const std::vector<Speleothem> lush = placeOver(lushProfile);
    bool sameButIce = glacier.size() == neutral.size();
    for (std::size_t n = 0; sameButIce && n < glacier.size(); ++n) {
        Speleothem iced = neutral[n];
        iced.ice = true;
        sameButIce = glacier[n] == iced;
    }
    const bool superset = std::all_of(neutral.begin(), neutral.end(), [&lush](const Speleothem& s) {
        return speleothems_test::contains(lush, s);
    });
    std::printf("glacier: %zu speleothems, %zu of ice (neutral: %zu of ice); lush: %zu\n",
                glacier.size(), countIce(glacier), countIce(neutral), lush.size());
    TEST_CHECK(countIce(neutral) == 0);
    TEST_CHECK(sameButIce && countIce(glacier) == glacier.size());
    TEST_CHECK(superset && lush.size() > neutral.size() && 2 * lush.size() < 3 * neutral.size());
}

// The speleothems of a square, placed alone or as a part of a larger one, are the same: the
// profile of a candidate is the one of its position, not of the area.
void testPlacementDoesNotDependOnTheArea() {
    const terrain::CaveEnvironment mild = environmentOf(terrain::Biome::Grassland, 12.0f, 0.5f);
    const CaveProfile profile = defaultProfile(CaveDistrictType::Dripstone);
    const Square part{-3000.0, 800.0, 64.0};
    const Square whole{-3032.0, 768.0, 128.0};
    const std::vector<Speleothem> alone = placeWith(dry(uniformCaves(profile)), mild, part);
    const std::vector<Speleothem> inside = placeWith(dry(uniformCaves(profile)), mild, whole);
    std::size_t found = 0;
    for (const Speleothem& s : inside) {
        const bool within = s.x >= part.minX && s.x < part.minX + part.size && s.z >= part.minZ &&
                            s.z < part.minZ + part.size;
        found += within && speleothems_test::contains(alone, s) ? 1 : 0;
    }
    std::printf("area: %zu speleothems alone, %zu of them inside a square four times larger\n",
                alone.size(), found);
    TEST_CHECK(alone.size() >= 20 && found == alone.size());
}

} // namespace

int main() {
    const std::vector<Speleothem> neutral = placeOver(CaveProfile{});
    testDripstone(neutral);
    testGlacierAndLush(neutral);
    testPlacementDoesNotDependOnTheArea();
    return test::result();
}
