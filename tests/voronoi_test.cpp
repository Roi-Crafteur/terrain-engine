#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/voronoi.hpp"

#include "test_assert.hpp"
#include "voronoi_test_support.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using terrain::Plate;
using terrain::PlateBoundary;
using terrain::PlateSample;
using terrain::TectonicPlates;
using terrain::VoronoiParams;
using voronoi_test::Points;
using voronoi_test::wrap;

constexpr std::array<std::uint64_t, 3> kSeeds = {terrain::kDefaultWorldSeed, 5, 11};

VoronoiParams unwarped() {
    VoronoiParams params;
    params.warpAmplitude = 0.0;
    params.detailWarpAmplitude = 0.0;
    return params;
}

bool sameSample(const PlateSample& a, const PlateSample& b) {
    return a.plate == b.plate && a.neighbor == b.neighbor &&
           a.boundaryDistance == b.boundaryDistance && a.level == b.level &&
           a.collision == b.collision && a.rift == b.rift && a.coast == b.coast &&
           a.continentalness == b.continentalness;
}

// Two layers built from the same seed give the same samples everywhere; another seed does not.
void testDeterminism() {
    for (const std::uint64_t seed : kSeeds) {
        const TectonicPlates a(seed, VoronoiParams{});
        const TectonicPlates b(seed, VoronoiParams{});
        const TectonicPlates other(seed + 1, VoronoiParams{});
        Points points(seed);
        bool same = true;
        int differ = 0;
        for (int i = 0; i < 500; ++i) {
            const double x = points.next(1.0e8);
            const double z = points.next(1.0e8);
            const PlateSample s = a.sampleAt(x, z);
            same = same && sameSample(s, b.sampleAt(x, z));
            differ += other.sampleAt(x, z).continentalness != s.continentalness ? 1 : 0;
        }
        TEST_CHECK(same);
        TEST_CHECK(differ > 250);
    }
}

// The nearest plate is the raw Voronoi cell of the warped point, and the second nearest one
// is always a neighbor: every pair of plates met in the world is a listed boundary.
void testNearestPairsAreBoundaries() {
    for (const std::uint64_t seed : kSeeds) {
        const TectonicPlates plates(seed, VoronoiParams{});
        Points points(seed + 7);
        bool consistent = true;
        for (int i = 0; i < 1500; ++i) {
            const double x = points.next(1.0e8);
            const double z = points.next(1.0e8);
            const PlateSample s = plates.sampleAt(x, z);
            const TectonicPlates::Point q = plates.warp(x, z);
            consistent = consistent && s.plate == plates.rawPlateAt(q.x, q.z) &&
                         s.neighbor != s.plate && plates.boundaryIndex(s.plate, s.neighbor) >= 0 &&
                         std::abs(s.boundaryDistance - voronoi_test::bisectorDistance(
                                                           plates, q, s.plate, s.neighbor)) < 1.0;
        }
        bool crossings = true;
        const auto found = voronoi_test::findCrossings(plates, seed, 30, 4.0e6, 50'000.0);
        for (const voronoi_test::Crossing& c : found) {
            crossings = crossings && plates.boundaryIndex(c.a, c.b) >= 0;
        }
        TEST_CHECK(consistent);
        TEST_CHECK(crossings);
        TEST_CHECK(found.size() > 10);
    }
}

// A point and its three 1 cm neighbors. Measured changes stay below 1e-7 over 1 cm; a real
// jump of a blend or an effect would be at least 1e-3.
bool continuousAt(const TectonicPlates& plates, double x, double z) {
    constexpr double kStep = 0.01;
    constexpr float kTolerance = 1.0e-5f;
    const PlateSample s = plates.sampleAt(x, z);
    const std::array<std::array<double, 2>, 3> offsets = {
        {{kStep, 0.0}, {0.0, kStep}, {-kStep, -kStep}}};
    bool continuous = std::isfinite(s.continentalness);
    for (const auto& offset : offsets) {
        const PlateSample n = plates.sampleAt(x + offset[0], z + offset[1]);
        continuous = continuous && std::abs(n.level - s.level) <= kTolerance &&
                     std::abs(n.collision - s.collision) <= kTolerance &&
                     std::abs(n.rift - s.rift) <= kTolerance &&
                     std::abs(n.coast - s.coast) <= kTolerance &&
                     std::abs(n.continentalness - s.continentalness) <= kTolerance;
    }
    return continuous;
}

// Junction of three plates a, b, c of the raw diagram (no warping): the circumcenter of
// their sites, if it really is a Voronoi vertex. Returns false otherwise.
bool junction(const TectonicPlates& plates, int a, int b, int c, double& x, double& z) {
    const double period = plates.planetSize();
    const Plate& pa = plates.plates()[static_cast<std::size_t>(a)];
    const Plate& pb = plates.plates()[static_cast<std::size_t>(b)];
    const Plate& pc = plates.plates()[static_cast<std::size_t>(c)];
    const double bx = wrap(pb.x - pa.x, period);
    const double bz = wrap(pb.z - pa.z, period);
    const double cx = wrap(pc.x - pa.x, period);
    const double cz = wrap(pc.z - pa.z, period);
    const double d = 2.0 * (bx * cz - bz * cx);
    const double b2 = bx * bx + bz * bz;
    const double c2 = cx * cx + cz * cz;
    x = pa.x + (cz * b2 - bz * c2) / d;
    z = pa.z + (bx * c2 - cx * b2) / d;
    const TectonicPlates::Point q{x, z};
    if (voronoi_test::bisectorDistance(plates, q, a, b) > 1.0 ||
        voronoi_test::bisectorDistance(plates, q, a, c) > 1.0) {
        return false;
    }
    for (int k = 0; k < static_cast<int>(plates.plates().size()); ++k) {
        if (k != a && k != b && k != c &&
            voronoi_test::signedBisectorDistance(plates, q, a, k) >= 0.0) {
            return false; // k is at least as close: not a vertex of a, b and c
        }
    }
    return true;
}

// Around every junction of three plates (without warping, where they are known exactly),
// from 30 cm to 120 km: the blends and the boundary effects meet without any jump.
int testJunctions(const TectonicPlates& plates) {
    int tested = 0;
    bool continuous = true;
    for (const PlateBoundary& ab : plates.boundaries()) {
        for (int c = ab.b + 1; c < static_cast<int>(plates.plates().size()); ++c) {
            double x = 0.0;
            double z = 0.0;
            if (plates.boundaryIndex(ab.a, c) < 0 || plates.boundaryIndex(ab.b, c) < 0 ||
                !junction(plates, ab.a, ab.b, c, x, z)) {
                continue;
            }
            ++tested;
            for (const double radius : {0.3, 1000.0, 30'000.0, 120'000.0}) {
                for (int k = 0; k < 8; ++k) {
                    const double angle = k * std::numbers::pi / 4.0 + 0.1;
                    continuous = continuous && continuousAt(plates, x + radius * std::cos(angle),
                                                            z + radius * std::sin(angle));
                }
            }
        }
    }
    TEST_CHECK(continuous);
    return tested;
}

void testContinuity() {
    for (const std::uint64_t seed : kSeeds) {
        const TectonicPlates plates(seed, VoronoiParams{});
        Points points(seed + 50);
        bool random = true;
        for (int i = 0; i < 2000; ++i) {
            random = random && continuousAt(plates, points.next(2.0e8), points.next(2.0e8));
        }
        TEST_CHECK(random);

        // On both sides of boundary crossings, where the nearest plate changes.
        bool crossings = true;
        for (const auto& c : voronoi_test::findCrossings(plates, seed + 3, 40, 4.0e6, 50'000.0)) {
            crossings = crossings && continuousAt(plates, c.x - 0.005 * c.dx, c.z - 0.005 * c.dz);
        }
        TEST_CHECK(crossings);

        const TectonicPlates raw(seed, unwarped());
        const int junctions = testJunctions(raw);
        TEST_CHECK(junctions >= 2 * static_cast<int>(raw.plates().size()) * 9 / 10);

        // Across the seams of the torus, x or z = k * planetSize.
        bool seams = true;
        for (int k = -2; k <= 2; ++k) {
            for (int i = 0; i < 100; ++i) {
                const double seam = k * raw.planetSize() - 0.005;
                const double along = points.next(1.0e8);
                seams = seams && continuousAt(raw, seam, along) && continuousAt(raw, along, seam);
            }
        }
        TEST_CHECK(seams);
    }
}

// Without warping, the plate layout repeats every planetSize along x and z: plates, belts,
// rifts and coasts. Only the coast roughness, an fbm of the world coordinates, does not.
void testPeriodicLayout() {
    const TectonicPlates raw(kSeeds[0], unwarped());
    const double period = raw.planetSize();
    Points points(3);
    bool periodic = true;
    for (int i = 0; i < 500; ++i) {
        const double x = points.next(period);
        const double z = points.next(period);
        const PlateSample s = raw.sampleAt(x, z);
        for (const auto& shift : {std::array<double, 2>{period, 0.0}, {0.0, -2.0 * period}}) {
            const PlateSample t = raw.sampleAt(x + shift[0], z + shift[1]);
            periodic =
                periodic && t.plate == s.plate && std::abs(t.collision - s.collision) < 1.0e-5f &&
                std::abs(t.rift - s.rift) < 1.0e-5f && std::abs(t.coast - s.coast) < 1.0e-5f &&
                (s.coast > 0.0f || std::abs(t.level - s.level) < 1.0e-5f);
        }
    }
    TEST_CHECK(periodic);
}

} // namespace

int main() {
    testDeterminism();
    testNearestPairsAreBoundaries();
    testContinuity();
    testPeriodicLayout();
    return test::result();
}
