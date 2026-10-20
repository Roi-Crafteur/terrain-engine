#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/voronoi.hpp"

#include "test_assert.hpp"
#include "voronoi_test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightfieldParams;
using terrain::HeightSample;
using terrain::PlateKind;
using terrain::PlateSample;
using terrain::TectonicPlates;
using voronoi_test::Points;

constexpr std::array<std::uint64_t, 3> kSeeds = {terrain::kDefaultWorldSeed, 5, 11};

// Seed stream of the plates among the layers of the Heightfield (the 8th one).
constexpr std::uint64_t kPlateLayer = 7;

// Every test sets the layers explicitly, so none depends on the default amplitudes.
HeightfieldParams layers(float plates, float macro) {
    HeightfieldParams params;
    params.plates.amplitude = plates;
    params.macroContinent = macro;
    params.macroContinentWavelength = 10'000'000.0;
    params.macroContinentWidth = 0.1f;
    return params;
}

bool sameSample(const HeightSample& a, const HeightSample& b) {
    return a.continentalness == b.continentalness &&
           a.macroContinentalness == b.macroContinentalness &&
           a.plateContinentalness == b.plateContinentalness &&
           a.plateCollision == b.plateCollision && a.mountainMask == b.mountainMask &&
           a.base == b.base && a.mountain == b.mountain && a.detail == b.detail &&
           a.height == b.height;
}

// Mountains fade in between these continentalness offsets above the coast (heightfield.cpp).
float landFactor(const HeightSample& s, const HeightfieldParams& params) {
    return terrain::noise::smoothstep(0.02f, 0.15f,
                                      s.continentalness - params.coastContinentalness);
}

// The layer is off by default. With an amplitude of 0 it is not even built (invalid plate
// parameters are not rejected) and nothing changes: no plate term, and the mountain mask is
// exactly the land factor times the massif fbm.
void testDisabledByDefault() {
    TEST_CHECK(HeightfieldParams{}.plates.amplitude == 0.0f);
    for (const std::uint64_t seed : kSeeds) {
        HeightfieldParams unused = layers(0.0f, 0.5f);
        unused.plates.minPlates = 1; // would throw if the plates were built
        unused.plates.cellDiameter = -1.0;
        const Heightfield off(seed, unused);
        const Heightfield reference(seed, layers(0.0f, 0.5f));
        TEST_CHECK(off.plates() == nullptr);
        TEST_CHECK(Heightfield(seed).plates() == nullptr);

        const HeightfieldParams& p = reference.params();
        const terrain::noise::FbmParams mask{.octaves = 3, .frequency = 1.0 / p.maskWavelength};
        const std::uint64_t maskSeed = terrain::noise::deriveSeed(seed, 1);
        Points points(seed);
        bool unchanged = true;
        for (int i = 0; i < 1000; ++i) {
            const double x = points.next(2.0e7);
            const double z = points.next(2.0e7);
            const HeightSample s = off.sampleAt(x, z);
            const float massif = terrain::noise::smoothstep(
                p.maskLow, p.maskHigh, terrain::noise::fbm2D(x, z, maskSeed, mask));
            unchanged = unchanged && sameSample(s, reference.sampleAt(x, z)) &&
                        s.plateContinentalness == 0.0f && s.plateCollision == 0.0f &&
                        s.mountainMask == landFactor(s, p) * massif;
        }
        TEST_CHECK(unchanged);
    }
}

// The plate term is added to the local and macro terms: continentalness = clamp(local +
// macro term + plates.amplitude * plate, -1, 1), with the plates of the seed's own stream.
void testMixing() {
    for (const std::uint64_t seed : kSeeds) {
        const HeightfieldParams params = layers(0.5f, 0.25f);
        const Heightfield both(seed, params);
        const Heightfield platesOnly(seed, layers(0.5f, 0.0f));
        const Heightfield local(seed, layers(0.0f, 0.0f));
        const TectonicPlates reference(terrain::noise::deriveSeed(seed, kPlateLayer),
                                       params.plates);
        TEST_CHECK(both.plates() != nullptr);
        TEST_CHECK(both.plates()->planetSize() == reference.planetSize());

        Points points(seed + 1);
        bool mixed = true;
        int macroShifts = 0;
        for (int i = 0; i < 1000; ++i) {
            const double x = points.next(1.0e8);
            const double z = points.next(1.0e8);
            const HeightSample s = both.sampleAt(x, z);
            const HeightSample p = platesOnly.sampleAt(x, z);
            const PlateSample plate = reference.sampleAt(x, z);
            const float base = local.sampleAt(x, z).continentalness;
            const float macro = 0.25f * std::tanh(s.macroContinentalness / 0.1f);
            const float term = 0.5f * plate.continentalness;
            mixed = mixed && s.plateContinentalness == plate.continentalness &&
                    s.plateCollision == plate.collision &&
                    std::abs(s.continentalness - std::clamp(base + macro + term, -1.0f, 1.0f)) <=
                        1.0e-6f &&
                    p.macroContinentalness == 0.0f &&
                    std::abs(p.continentalness - std::clamp(base + term, -1.0f, 1.0f)) <= 1.0e-6f;
            macroShifts += s.continentalness != p.continentalness ? 1 : 0;
        }
        TEST_CHECK(mixed);
        TEST_CHECK(macroShifts > 900); // the macro layer still contributes
    }
}

// Collision belts lift the mountain mask (mask >= land factor * collision everywhere), and
// the ridged noise, still only under the mask and boosted in the belts, raises ranges along
// them: on land, the mountains of the belts are much higher than elsewhere (measured: 2.8
// times on average; 2.0 times without the boost).
void testMountainRanges() {
    double beltMountain = 0.0;
    int belt = 0;
    double otherMountain = 0.0;
    int other = 0;
    bool masked = true;
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield field(seed, layers(0.5f, 0.25f));
        const TectonicPlates& plates = *field.plates();
        std::vector<std::array<double, 2>> points;
        for (const auto& c : voronoi_test::findCrossings(plates, seed, 60, 4.0e6, 50'000.0)) {
            points.push_back({c.x, c.z});
        }
        Points random(seed + 2);
        for (int i = 0; i < 3000; ++i) {
            points.push_back({random.next(1.0e8), random.next(1.0e8)});
        }
        for (const auto& [x, z] : points) {
            const HeightSample s = field.sampleAt(x, z);
            masked = masked && s.mountainMask >= landFactor(s, field.params()) * s.plateCollision &&
                     (s.mountainMask > 0.0f || s.mountain == 0.0f);
            if (landFactor(s, field.params()) < 1.0f) {
                continue; // coasts and sea
            }
            (s.plateCollision > 0.5f ? beltMountain : otherMountain) += s.mountain;
            ++(s.plateCollision > 0.5f ? belt : other);
        }
    }
    TEST_CHECK(masked);
    TEST_CHECK(belt > 20 && other > 1000);
    TEST_CHECK(beltMountain / belt > 2.0 * (otherMountain / other));
}

// A point and its three 1 cm neighbors, as in heightfield_macro_test: no jump of the height
// or of the plate term.
bool continuousAt(const Heightfield& field, double x, double z) {
    constexpr double kMaxSlope = 10.0;
    constexpr double kStep = 0.01;
    constexpr double kTolerance = kMaxSlope * kStep + 1.0e-3; // + float rounding
    constexpr float kPlateTolerance = 1.0e-5f;
    const HeightSample s = field.sampleAt(x, z);
    bool continuous = std::isfinite(s.height);
    for (const auto& [dx, dz] :
         {std::array<double, 2>{kStep, 0.0}, {0.0, kStep}, {-kStep, -kStep}}) {
        const HeightSample n = field.sampleAt(x + dx, z + dz);
        continuous = continuous && std::abs(n.height - s.height) <= 2.0 * kTolerance &&
                     std::abs(n.plateContinentalness - s.plateContinentalness) <= kPlateTolerance &&
                     std::abs(n.plateCollision - s.plateCollision) <= kPlateTolerance;
    }
    return continuous;
}

void testContinuity() {
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield field(seed, layers(0.5f, 0.25f));
        Points points(seed + 3);
        bool random = true;
        for (int i = 0; i < 3000; ++i) {
            random = random && continuousAt(field, points.next(1.0e8), points.next(1.0e8));
        }
        bool crossings = true;
        for (const auto& c :
             voronoi_test::findCrossings(*field.plates(), seed + 4, 30, 4.0e6, 50'000.0)) {
            crossings = crossings && continuousAt(field, c.x - 0.005 * c.dx, c.z - 0.005 * c.dz);
        }
        TEST_CHECK(random);
        TEST_CHECK(crossings);
    }
}

// The same seed and parameters give the same world; copies share the same plates.
void testDeterminism() {
    const Heightfield a(kSeeds[0], layers(0.5f, 0.25f));
    const Heightfield b(kSeeds[0], layers(0.5f, 0.25f));
    const Heightfield copy = a;
    const Heightfield other(kSeeds[1], layers(0.5f, 0.25f));
    TEST_CHECK(copy.plates() == a.plates());
    Points points(9);
    bool same = true;
    int differ = 0;
    for (int i = 0; i < 1000; ++i) {
        const double x = points.next(1.0e8);
        const double z = points.next(1.0e8);
        const HeightSample s = a.sampleAt(x, z);
        same = same && sameSample(s, b.sampleAt(x, z)) && sameSample(s, copy.sampleAt(x, z));
        differ += other.sampleAt(x, z).height != s.height ? 1 : 0;
    }
    TEST_CHECK(same);
    TEST_CHECK(differ > 900);
}

// Over the whole planet (96 x 96 samples of one period), oceans and continents are both
// large: the interiors of continent plates are land, those of ocean plates are sea. The
// 32,768 km zoom around the origin holds both as well.
void testPlanet() {
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield field(seed, layers(0.5f, 0.25f));
        const TectonicPlates& plates = *field.plates();
        constexpr int kSide = 96;
        const double step = plates.planetSize() / kSide;
        int land = 0;
        std::array<int, 2> continent{}; // land, samples, in continent interiors
        std::array<int, 2> ocean{};
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                const double x = (i + 0.5) * step;
                const double z = (j + 0.5) * step;
                const bool isLand = field.heightAt(x, z) >= terrain::kSeaLevel;
                land += isLand ? 1 : 0;
                const TectonicPlates::Point q = plates.warp(x, z);
                const int plate = plates.rawPlateAt(q.x, q.z);
                if (voronoi_test::thirdPlateDistance(plates, q, plate, -1) < 1'000'000.0) {
                    continue;
                }
                const PlateKind kind = plates.plates()[static_cast<std::size_t>(plate)].kind;
                std::array<int, 2>* interior = kind == PlateKind::Continent ? &continent
                                               : kind == PlateKind::Ocean   ? &ocean
                                                                            : nullptr;
                if (interior != nullptr) {
                    (*interior)[0] += isLand ? 1 : 0;
                    ++(*interior)[1];
                }
            }
        }
        const double share = static_cast<double>(land) / (kSide * kSide);
        TEST_CHECK(share > 0.15 && share < 0.65);
        TEST_CHECK(continent[1] > 100 && ocean[1] > 100);
        TEST_CHECK(continent[0] > continent[1] * 85 / 100);
        TEST_CHECK(ocean[0] < ocean[1] / 10);
    }

    const Heightfield field(terrain::kDefaultWorldSeed, layers(0.5f, 0.25f));
    int land = 0;
    for (int j = 0; j < 64; ++j) {
        for (int i = 0; i < 64; ++i) {
            const double x = (i + 0.5 - 32.0) * 512'000.0;
            const double z = (j + 0.5 - 32.0) * 512'000.0;
            land += field.heightAt(x, z) >= terrain::kSeaLevel ? 1 : 0;
        }
    }
    TEST_CHECK(land > 64 * 64 / 10 && land < 64 * 64 * 9 / 10);
}

} // namespace

int main() {
    testDisabledByDefault();
    testMixing();
    testMountainRanges();
    testContinuity();
    testDeterminism();
    testPlanet();
    return test::result();
}
