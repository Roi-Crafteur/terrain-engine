// Speleothems: deterministic objects laid in the caves, the same ones whatever rectangle is
// placed (the union of adjacent squares), the caves never changed by them, each anchored in the
// rock of a ceiling or a floor with its tip in the air, above the water of the lakes, never in
// the void a column opens to the sky, rarer under dry climates and icy under cold ones. Every
// parameter is read from the field.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/speleothems.hpp"

#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "speleothems_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <vector>

namespace {

using caves_test::carvedAt;
using caves_test::coldEnvironment;
using caves_test::environmentOf;
using caves_test::flatGround;
using caves_test::heightfieldGround;
using caves_test::humidEnvironment;
using caves_test::kSeed;
using caves_test::Point;
using caves_test::randomPoints;
using caves_test::Rng;
using caves_test::withEnvironment;
using speleothems_test::anchoring;
using speleothems_test::Anchoring;
using speleothems_test::aquiferParams;
using speleothems_test::contains;
using speleothems_test::countAroundEntrance;
using speleothems_test::countIce;
using speleothems_test::countKind;
using speleothems_test::kFlatHeight;
using speleothems_test::placeIn;
using speleothems_test::placeWith;
using speleothems_test::samplerOver;
using speleothems_test::SkyCounts;
using speleothems_test::sorted;
using speleothems_test::Square;
using terrain::Biome;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveSampler;
using terrain::Heightfield;
using terrain::Speleothem;
using terrain::SpeleothemField;
using terrain::SpeleothemKind;
using terrain::SpeleothemParams;

// ---------------------------------------------------------------------------

// Same seed, same speleothems, from any sampler; another seed, others.
void testDeterministic() {
    const CaveField field(kSeed);
    const CaveGround ground = withEnvironment(flatGround(kFlatHeight), humidEnvironment());
    const SpeleothemParams& p = field.params().speleothems;
    const SpeleothemField a(kSeed, p);
    const SpeleothemField b(kSeed, p);
    const SpeleothemField c(kSeed + 1, p);
    const Square square{0.0, 0.0, 64.0};
    CaveSampler first = samplerOver(field, ground, square);
    CaveSampler second = samplerOver(field, ground, square);
    const std::vector<Speleothem> list = placeIn(a, first, square);
    const std::vector<Speleothem> other = placeIn(c, first, square);
    std::size_t common = 0;
    for (const Speleothem& s : other) {
        common += std::find(list.begin(), list.end(), s) != list.end() ? 1 : 0;
    }
    std::printf("determinism: %zu stalactites and %zu stalagmites over 64 x 64 m, %zu with "
                "seed + 1 (%zu in common)\n",
                countKind(list, SpeleothemKind::Stalactite),
                countKind(list, SpeleothemKind::Stalagmite), other.size(), common);
    TEST_CHECK(list.size() >= 40);
    TEST_CHECK(list == placeIn(a, first, square));
    TEST_CHECK(list == placeIn(b, second, square));
    TEST_CHECK(list != other && !other.empty());
    TEST_CHECK(common * 20 <= list.size());
}

// A square placed at once gives the speleothems of its 4 quarters placed apart (each from its
// own sampler), once sorted.
void checkUnion(const CaveField& field, const CaveGround& ground, const Square& s,
                const char* name) {
    const SpeleothemField spel(kSeed, field.params().speleothems);
    CaveSampler whole = samplerOver(field, ground, s);
    const std::vector<Speleothem> all = sorted(placeIn(spel, whole, s));
    std::vector<Speleothem> parts;
    std::size_t smallest = all.size();
    const double half = s.size / 2.0;
    for (int n = 0; n < 4; ++n) {
        const Square quarter{s.minX + (n % 2) * half, s.minZ + (n / 2) * half, half};
        CaveSampler sampler = samplerOver(field, ground, quarter);
        const std::vector<Speleothem> part = placeIn(spel, sampler, quarter);
        smallest = std::min(smallest, part.size());
        parts.insert(parts.end(), part.begin(), part.end());
    }
    std::printf("union (%s): %zu speleothems over %.0f x %.0f m, %zu in the 4 quarters (the "
                "smallest holds %zu)\n",
                name, all.size(), s.size, s.size, parts.size(), smallest);
    TEST_CHECK(all.size() >= 20);
    TEST_CHECK(smallest > 0);
    TEST_CHECK(all == sorted(parts));
}

void testUnion() {
    const CaveField field(kSeed);
    const Heightfield heightfield(kSeed);
    checkUnion(field, withEnvironment(flatGround(kFlatHeight), humidEnvironment()),
               {-16.0, -16.0, 32.0}, "flat");
    checkUnion(field, withEnvironment(heightfieldGround(heightfield), humidEnvironment()),
               {1000.0, -2000.0, 128.0}, "relief");
}

// Placing never mutates the caves: the carved density at points of the area is bit-identical
// before and after, from the same sampler and from a fresh one, whatever the parameters of the
// speleothems.
void testGeometryUnchanged() {
    const CaveField field(kSeed);
    CaveParams dense;
    dense.speleothems.chance = 1.0f;
    dense.speleothems.dryFactor = 1.0f;
    dense.speleothems.spacing = 2.0;
    const CaveField denseField(kSeed, dense);
    const CaveGround ground = withEnvironment(flatGround(kFlatHeight), humidEnvironment());
    const Square square{-16.0, -16.0, 32.0};
    Rng rng(9);
    const std::vector<Point> points = randomPoints(ground, rng, 1500, {0.0, 0.0, 16.0}, 0.5, 96.0);
    CaveSampler sampler = samplerOver(field, ground, square);
    std::vector<float> before;
    for (const Point& p : points) {
        before.push_back(carvedAt(sampler, p));
    }
    const SpeleothemField spel(kSeed, field.params().speleothems);
    const SpeleothemField crowded(kSeed, dense.speleothems);
    const std::size_t placed = placeIn(spel, sampler, square).size();
    const std::size_t packed = placeIn(crowded, sampler, square).size();
    CaveSampler fresh = samplerOver(field, ground, square);
    CaveSampler other = samplerOver(denseField, ground, square);
    bool same = true;
    bool fresher = true;
    bool crowdedSame = true;
    for (std::size_t n = 0; n < points.size(); ++n) {
        same = same && carvedAt(sampler, points[n]) == before[n];
        fresher = fresher && carvedAt(fresh, points[n]) == before[n];
        crowdedSame = crowdedSame && carvedAt(other, points[n]) == before[n];
    }
    std::printf("geometry: %zu points, %zu speleothems placed (%zu with dense parameters), the "
                "carved density never changes\n",
                points.size(), placed, packed);
    TEST_CHECK(placed > 0 && packed > placed);
    TEST_CHECK(same && fresher && crowdedSame);
}

// ---------------------------------------------------------------------------

void checkAnchoring(const CaveField& field, const CaveGround& ground, const Square& s,
                    const char* name, bool lakes) {
    const Anchoring a = anchoring(field, ground, s);
    std::printf("anchoring (%s): %zu stalactites (%zu over a lake), %zu stalagmites\n", name,
                a.stalactites, a.flooded, a.stalagmites);
    TEST_CHECK(a.stalactites >= 20 && a.stalagmites >= 5);
    TEST_CHECK(a.badBase == 0 && a.badTip == 0);
    TEST_CHECK(a.badLength == 0 && a.badRadius == 0);
    TEST_CHECK(a.badPair == 0 && a.underWater == 0 && a.badIce == 0);
    TEST_CHECK(!lakes || a.flooded > 0);
}

// Each stalactite hangs from rock with air below and its tip in the air; each stalagmite rises
// from a floor with air above; lengths stay within their share of the room (down to the water
// of a lake); the two of a candidate never touch; nothing lies under the water.
void testAnchoring() {
    const Heightfield heightfield(kSeed);
    const CaveField field(kSeed);
    const CaveField lakes(kSeed, aquiferParams(true));
    const Square square{-32.0, -32.0, 64.0};
    checkAnchoring(field, withEnvironment(flatGround(kFlatHeight), humidEnvironment()), square,
                   "flat", false);
    checkAnchoring(lakes, withEnvironment(flatGround(kFlatHeight), humidEnvironment()),
                   {-64.0, -64.0, 128.0}, "flat, lakes everywhere", true);
    checkAnchoring(field, withEnvironment(heightfieldGround(heightfield), humidEnvironment()),
                   {1000.0, -2000.0, 192.0}, "relief", false);
    checkAnchoring(field, withEnvironment(flatGround(kFlatHeight), coldEnvironment()), square,
                   "cold", false);
}

// ---------------------------------------------------------------------------

// A void that a column opens to the sky (air 1 m under the ground) holds no speleothem: with a
// candidate in every square meter around the tops of entrances, the columns that open to the
// sky have speleothems only in the voids below their rock, and far fewer than the others. (The
// rule is per column: the roof of the tube of an entrance, once underground, may hold some.)
void testOpenToSky() {
    CaveParams params = aquiferParams(false);
    params.speleothems.spacing = 1.0;
    params.speleothems.chance = 1.0f;
    params.speleothems.dryFactor = 1.0f;
    const CaveField field(kSeed, params);
    const SpeleothemField spel(kSeed, params.speleothems);
    const CaveGround ground = withEnvironment(flatGround(kFlatHeight), humidEnvironment());
    const std::vector<CaveEntrance> entrances =
        field.entrancesNear(-2048.0, -2048.0, 2048.0, 2048.0, ground);
    SkyCounts counts;
    std::size_t used = 0;
    for (std::size_t n = 0; n < entrances.size() && used < 3; ++n, ++used) {
        countAroundEntrance(field, spel, ground, entrances[n], counts);
    }
    const double openShare =
        static_cast<double>(counts.inOpen) / static_cast<double>(counts.columns);
    const double closedShare =
        static_cast<double>(counts.inClosed) / static_cast<double>(counts.closed);
    std::printf("open to the sky: %zu entrances, %zu open columns holding %zu speleothems (%zu in "
                "their sky void), %zu other columns holding %zu\n",
                used, counts.columns, counts.inOpen, counts.sharing, counts.closed,
                counts.inClosed);
    TEST_CHECK(used == 3);
    TEST_CHECK(counts.columns >= 100);
    TEST_CHECK(counts.inClosed >= 100);
    TEST_CHECK(counts.sharing == 0);
    TEST_CHECK(openShare * 4.0 < closedShare);
}

// ---------------------------------------------------------------------------

// Dry climates thin the candidates: deserts, savannas and beaches keep dryFactor of them
// whatever the humidity, other biomes from dryFactor to all as the humidity rises. Without
// lakes the thinned ones are a subset of the others, at the same places (desert, then half
// way, then forest).
void testFrequency() {
    const CaveParams noLakes = aquiferParams(false);
    const SpeleothemParams& p = noLakes.speleothems;
    const Square square{-64.0, -64.0, 128.0};
    const auto in = [](Biome biome, float humidity) {
        return environmentOf(biome, 12.0f, humidity);
    };
    const std::vector<Speleothem> desert = placeWith(noLakes, in(Biome::Desert, 0.05f), square);
    const std::vector<Speleothem> forest =
        placeWith(noLakes, in(Biome::TemperateForest, 0.9f), square);
    const std::vector<Speleothem> middle =
        placeWith(noLakes, in(Biome::Grassland, (p.dryHumidity + p.wetHumidity) / 2.0f), square);
    bool nested = true;
    for (const Speleothem& s : desert) {
        nested = nested && contains(middle, s);
    }
    for (const Speleothem& s : middle) {
        nested = nested && contains(forest, s);
    }
    const double ratio = static_cast<double>(desert.size()) / static_cast<double>(forest.size());
    const double halfway = static_cast<double>(middle.size()) / static_cast<double>(forest.size());
    std::printf("frequency: %zu speleothems in a desert, %zu in a temperate forest, %zu between "
                "(ratios %.2f and %.2f, dry factor %.2f)\n",
                desert.size(), forest.size(), middle.size(), ratio, halfway,
                static_cast<double>(p.dryFactor));
    TEST_CHECK(forest.size() >= 100);
    TEST_CHECK(nested);
    TEST_CHECK(std::abs(ratio - static_cast<double>(p.dryFactor)) < 0.1);
    TEST_CHECK(std::abs(halfway - static_cast<double>((p.dryFactor + 1.0f) / 2.0f)) < 0.1);
    TEST_CHECK(desert.size() < middle.size() && middle.size() < forest.size());
    // The dry biomes keep the same ones whatever the humidity, and a dry humidity gives them to
    // the other biomes.
    TEST_CHECK(desert == placeWith(noLakes, in(Biome::Desert, 0.9f), square));
    TEST_CHECK(desert == placeWith(noLakes, in(Biome::Savanna, 0.9f), square));
    TEST_CHECK(desert == placeWith(noLakes, in(Biome::Beach, 0.5f), square));
    TEST_CHECK(desert == placeWith(noLakes, in(Biome::Grassland, p.dryHumidity), square));
    // With the lakes of the default climate, the desert still has clearly fewer.
    const CaveParams standard;
    const std::size_t dry = placeWith(standard, in(Biome::Desert, 0.05f), square).size();
    const std::size_t wet = placeWith(standard, in(Biome::TemperateForest, 0.9f), square).size();
    std::printf("frequency with the lakes of the climate: %zu in a desert, %zu in a forest\n", dry,
                wet);
    TEST_CHECK(dry * 2 < wet);
}

// Ice: colder than iceTemperature at the surface, or over a frozen lake.
void testIce() {
    const CaveParams noLakes = aquiferParams(false);
    const CaveParams lakes = aquiferParams(true);
    const float edge = noLakes.speleothems.iceTemperature;
    const float freezing = lakes.aquifers.freezeTemperature;
    const Square square{-32.0, -32.0, 64.0};
    const auto at = [&](const CaveParams& params, float temperature) {
        return placeWith(params, environmentOf(Biome::Taiga, temperature, 0.9f), square);
    };
    const std::vector<Speleothem> cold = at(noLakes, -10.0f);
    const std::vector<Speleothem> warm = at(noLakes, 12.0f);
    const std::vector<Speleothem> onEdge = at(noLakes, edge);
    const std::vector<Speleothem> above = at(noLakes, std::nextafter(edge, 100.0f));
    const std::vector<Speleothem> frozen = at(lakes, (edge + freezing) / 2.0f);
    const std::vector<Speleothem> thawed = at(lakes, freezing + 1.0f);
    std::printf("ice: %zu of %zu in the cold, %zu of %zu in the warmth, %zu of %zu over frozen "
                "lakes, %zu of %zu over thawed ones\n",
                countIce(cold), cold.size(), countIce(warm), warm.size(), countIce(frozen),
                frozen.size(), countIce(thawed), thawed.size());
    TEST_CHECK(cold.size() >= 40 && warm.size() >= 40);
    TEST_CHECK(countIce(cold) == cold.size() && countIce(warm) == 0);
    TEST_CHECK(countIce(onEdge) == onEdge.size() && !onEdge.empty());
    TEST_CHECK(countIce(above) == 0 && !above.empty());
    TEST_CHECK(frozen.size() >= 10 && countIce(frozen) == frozen.size());
    TEST_CHECK(thawed.size() >= 10 && countIce(thawed) == 0);
}

} // namespace

int main() {
    testDeterministic();
    testUnion();
    testGeometryUnchanged();
    testAnchoring();
    testOpenToSky();
    testFrequency();
    testIce();
    return test::result();
}
