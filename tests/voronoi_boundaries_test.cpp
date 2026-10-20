#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/voronoi.hpp"

#include "test_assert.hpp"
#include "voronoi_test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

using terrain::BoundaryKind;
using terrain::PlateBoundary;
using terrain::PlateKind;
using terrain::PlateSample;
using terrain::TectonicPlates;
using terrain::VoronoiParams;
using voronoi_test::Crossing;
using voronoi_test::Points;

constexpr std::array<std::uint64_t, 3> kSeeds = {terrain::kDefaultWorldSeed, 5, 11};

VoronoiParams unwarped() {
    VoronoiParams params;
    params.warpAmplitude = 0.0;
    params.detailWarpAmplitude = 0.0;
    return params;
}

std::vector<Crossing> crossingsOf(const TectonicPlates& plates, std::uint64_t seed) {
    return voronoi_test::findCrossings(plates, seed, 40, 4.0e6, 50'000.0);
}

// The crossing from plate a to plate b on the line through (x, z) along (dx, dz), the
// nearest to (x, z) within 400 km, if any.
std::optional<Crossing> crossingNear(const TectonicPlates& plates, double x, double z, double dx,
                                     double dz, int a, int b) {
    constexpr double kReach = 400'000.0;
    constexpr double kStep = 10'000.0;
    std::optional<Crossing> best;
    int previous = voronoi_test::plateAt(plates, x - kReach * dx, z - kReach * dz);
    for (double t = -kReach + kStep; t <= kReach; t += kStep) {
        const int plate = voronoi_test::plateAt(plates, x + t * dx, z + t * dz);
        if (previous == a && plate == b &&
            (!best || std::abs(t) < std::hypot(best->x - x, best->z - z))) {
            best = voronoi_test::bisect(plates, x, z, dx, dz, t - kStep, t);
        }
        previous = plate;
    }
    return best;
}

// Sagitta of a boundary at a crossing: the same boundary is found on the parallel lines
// 200 km to each side, and the distance from the crossing to the chord between those two
// points is measured. 0 for a straight boundary. Returns -1 if a side is missing.
double sagitta(const TectonicPlates& plates, const Crossing& c) {
    constexpr double kOffset = 200'000.0;
    const double nx = -c.dz;
    const double nz = c.dx;
    const auto before =
        crossingNear(plates, c.x - kOffset * nx, c.z - kOffset * nz, c.dx, c.dz, c.a, c.b);
    const auto after =
        crossingNear(plates, c.x + kOffset * nx, c.z + kOffset * nz, c.dx, c.dz, c.a, c.b);
    if (!before || !after) {
        return -1.0;
    }
    const double chordX = after->x - before->x;
    const double chordZ = after->z - before->z;
    const double cross = (c.x - before->x) * chordZ - (c.z - before->z) * chordX;
    return std::abs(cross) / std::hypot(chordX, chordZ);
}

struct Bending {
    double meanDeviation = 0.0; // from the straight bisector of the raw sites
    double maxDeviation = 0.0;
    double meanSagitta = 0.0;
    double maxSagitta = 0.0;
};

Bending measureBending(const TectonicPlates& plates, std::uint64_t seed) {
    Bending bending;
    int deviations = 0;
    int sagittas = 0;
    for (const Crossing& c : crossingsOf(plates, seed)) {
        const double deviation = voronoi_test::bisectorDistance(plates, {c.x, c.z}, c.a, c.b);
        bending.meanDeviation += deviation;
        bending.maxDeviation = std::max(bending.maxDeviation, deviation);
        ++deviations;
        const double s = sagitta(plates, c);
        if (s >= 0.0) {
            bending.meanSagitta += s;
            bending.maxSagitta = std::max(bending.maxSagitta, s);
            ++sagittas;
        }
    }
    bending.meanDeviation /= std::max(deviations, 1);
    bending.meanSagitta /= std::max(sagittas, 1);
    TEST_CHECK(deviations > 10 && sagittas > 10);
    return bending;
}

// No raw polygons: without warping every boundary is the straight bisector of two sites; with
// it, the boundaries move hundreds of kilometers away from those bisectors and are curved
// everywhere, not only displaced. Measured: mean deviations of 113 to 143 km (max 363 to
// 547 km) and mean sagittas of 8 to 15 km; the same measures on the raw diagram stay below
// 1 m.
void testNoRawPolygons() {
    for (const std::uint64_t seed : kSeeds) {
        const Bending raw = measureBending(TectonicPlates(seed, unwarped()), seed);
        const Bending warped = measureBending(TectonicPlates(seed, VoronoiParams{}), seed);
        TEST_CHECK(raw.maxDeviation < 1.0);
        TEST_CHECK(raw.maxSagitta < 1.0);
        TEST_CHECK(warped.meanDeviation > 80'000.0);
        TEST_CHECK(warped.maxDeviation > 250'000.0);
        TEST_CHECK(warped.meanSagitta > 3'000.0);
    }
}

float kindLevel(const VoronoiParams& p, PlateKind kind) {
    switch (kind) {
    case PlateKind::Continent:
        return p.continentLevel;
    case PlateKind::InlandSea:
        return p.inlandSeaLevel;
    case PlateKind::ContinentalShelf:
        return p.shelfLevel;
    case PlateKind::Ocean:
        break;
    }
    return p.oceanLevel;
}

// Far from every boundary (1,000 km) a plate is its own kind: no belt, rift or coast, and
// exactly the level of its kind.
void testInteriors(const TectonicPlates& plates, std::uint64_t seed) {
    Points points(seed + 21);
    int interiors = 0;
    bool plain = true;
    for (int i = 0; i < 1500; ++i) {
        const double x = points.next(1.0e8);
        const double z = points.next(1.0e8);
        const TectonicPlates::Point q = plates.warp(x, z);
        const int plate = plates.rawPlateAt(q.x, q.z);
        if (voronoi_test::thirdPlateDistance(plates, q, plate, -1) < 1'000'000.0) {
            continue;
        }
        ++interiors;
        const PlateSample s = plates.sampleAt(x, z);
        const PlateKind kind = plates.plates()[static_cast<std::size_t>(plate)].kind;
        plain = plain && s.collision == 0.0f && s.rift == 0.0f && s.coast == 0.0f &&
                s.level == kindLevel(plates.params(), kind) && s.continentalness == s.level;
    }
    TEST_CHECK(plain);
    TEST_CHECK(interiors > 200);
}

// On a boundary, away from any third plate, only the effect of its kind shows: a belt of
// the boundary strength on collisions, a rift on divergences (a rift valley next to land, a
// mid-ocean ridge between water plates), nothing on transforms; a full coast between land
// and water and none elsewhere, where the level is the mean of both plates.
std::array<int, 3> testBoundaryEffects(const TectonicPlates& plates, std::uint64_t seed) {
    const VoronoiParams& p = plates.params();
    std::array<int, 3> tested{};
    int coasts = 0;
    bool effects = true;
    for (const Crossing& c : crossingsOf(plates, seed + 1)) {
        if (voronoi_test::thirdPlateDistance(plates, plates.warp(c.x, c.z), c.a, c.b) < 800'000.0) {
            continue;
        }
        const PlateSample s = plates.sampleAt(c.x, c.z);
        const PlateBoundary& b =
            plates.boundaries()[static_cast<std::size_t>(plates.boundaryIndex(c.a, c.b))];
        const float strength = std::abs(b.convergence);
        const PlateKind kindA = plates.plates()[static_cast<std::size_t>(c.a)].kind;
        const PlateKind kindB = plates.plates()[static_cast<std::size_t>(c.b)].kind;
        const bool landA = kindA == PlateKind::Continent;
        const bool landB = kindB == PlateKind::Continent;
        ++tested[static_cast<std::size_t>(b.kind)];
        if (b.kind == BoundaryKind::Collision) {
            effects = effects && std::abs(s.collision - strength) < 1.0e-3f && s.rift == 0.0f;
        } else if (b.kind == BoundaryKind::Divergence) {
            const float flank = landA || landB ? p.riftFlankHeight : p.oceanRidgeHeight;
            const float rift = (flank - p.riftDepth) * strength;
            effects = effects && s.collision == 0.0f && std::abs(s.rift - rift) < 1.0e-3f;
        } else {
            effects = effects && s.collision == 0.0f && s.rift == 0.0f;
        }
        if (landA != landB) {
            effects = effects && s.coast > 0.999f;
            ++coasts;
        } else {
            const float mean = 0.5f * (kindLevel(p, kindA) + kindLevel(p, kindB));
            effects = effects && s.coast == 0.0f && std::abs(s.level - mean) < 1.0e-4f;
        }
    }
    TEST_CHECK(effects);
    TEST_CHECK(coasts > 0);
    return tested;
}

void testEffectsLocalized() {
    std::array<int, 3> tested{};
    for (const std::uint64_t seed : kSeeds) {
        const TectonicPlates plates(seed, VoronoiParams{});
        testInteriors(plates, seed);
        const std::array<int, 3> kinds = testBoundaryEffects(plates, seed);
        for (std::size_t k = 0; k < tested.size(); ++k) {
            tested[k] += kinds[k];
        }
    }
    TEST_CHECK(tested[0] > 0 && tested[1] > 0 && tested[2] > 0); // every boundary kind
}

} // namespace

int main() {
    testNoRawPolygons();
    testEffectsLocalized();
    return test::result();
}
