// Arches on their own: deterministic candidates of the jittered grid, and the signed
// distance fields of the half-torus, the pillars and the opening below. Arches placed in a
// region: sculpted_region_test.cpp. Every parameter is read from the field.
#include "terrain/generation/arches.hpp"

#include "terrain/chunk_coord.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::Arch;
using terrain::ArchField;
using terrain::ArchParams;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// An arch of half-span 25 m standing at 100 m, its span along (0.6, 0.8).
Arch makeArch() {
    Arch arch;
    arch.x = 1000.0;
    arch.z = -2000.0;
    arch.directionX = 0.6;
    arch.directionZ = 0.8;
    arch.halfSpan = 25.0f;
    arch.base = 100.0f;
    return arch;
}

struct Point {
    double x;
    double z;
};

// World position at (along, across) from the center of the arch.
Point at(const Arch& arch, double along, double across) {
    return {arch.x + along * arch.directionX - across * arch.directionZ,
            arch.z + along * arch.directionZ + across * arch.directionX};
}

// ---------------------------------------------------------------------------

// Same seed and cell: same candidate. The center stays in the jittered part of its cell,
// the direction is a unit vector, the half-span is in range, and about `chance` of the
// cells have a candidate.
void testCandidates() {
    const ArchField field(kSeed);
    const ArchField again(kSeed);
    const ArchField other(kSeed + 1);
    const ArchParams& p = field.params();
    int found = 0;
    bool same = true;
    bool differs = false;
    bool valid = true;
    for (int cellZ = -10; cellZ < 10; ++cellZ) {
        for (int cellX = -10; cellX < 10; ++cellX) {
            Arch a;
            Arch b;
            Arch c;
            const bool has = field.candidate(cellX, cellZ, a);
            same = same && has == again.candidate(cellX, cellZ, b) && (!has || a == b);
            const bool otherHas = other.candidate(cellX, cellZ, c);
            differs = differs || has != otherHas || (has && !(a == c));
            if (!has) {
                continue;
            }
            ++found;
            const double low = 0.5 - 0.5 * p.jitter;
            const double high = 0.5 + 0.5 * p.jitter;
            const double u = a.x / p.spacing - cellX;
            const double v = a.z / p.spacing - cellZ;
            valid = valid && u >= low && u <= high && v >= low && v <= high &&
                    std::abs(std::hypot(a.directionX, a.directionZ) - 1.0) < 1.0e-9 &&
                    a.halfSpan >= p.minHalfSpan && a.halfSpan <= p.maxHalfSpan &&
                    a.base == 0.0f && a.cellX == cellX && a.cellZ == cellZ;
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(differs);
    TEST_CHECK(valid);
    const double share = found / 400.0;
    TEST_CHECK(std::abs(share - static_cast<double>(p.chance)) < 0.1);
}

// Heights of the arch: pillars up to the springline, then the half-torus.
void testHeights() {
    const ArchField field(kSeed);
    const ArchParams& p = field.params();
    const Arch arch = makeArch();
    TEST_CHECK(field.springline(arch) == arch.base + p.pillarHeight);
    TEST_CHECK(field.crown(arch) == field.springline(arch) + arch.halfSpan + p.tubeRadius);
    TEST_CHECK(field.openingTop(arch) == field.springline(arch) + arch.halfSpan - p.tubeRadius);
    TEST_CHECK(field.reach(arch) == field.boundingRadius(arch) + p.reachMargin);
    TEST_CHECK(p.pillarRadius >= p.tubeRadius);
}

// Rock at the crown and in the pillars, air above the crown and beside the pillars.
void testRock() {
    const ArchField field(kSeed);
    const ArchParams& p = field.params();
    const Arch arch = makeArch();
    const double spring = field.springline(arch);
    const Point center = at(arch, 0.0, 0.0);
    const double tube = spring + arch.halfSpan; // axis of the tube at the crown
    TEST_CHECK(field.rock(arch, center.x, tube, center.z) < 0.0f);
    TEST_CHECK(field.rock(arch, center.x, field.crown(arch) + 0.5, center.z) > 0.0f);
    TEST_CHECK(field.rock(arch, center.x, field.openingTop(arch) - 0.5, center.z) > 0.0f);
    const Point across = at(arch, 0.0, p.tubeRadius + 0.5); // beside the crown
    TEST_CHECK(field.rock(arch, across.x, tube, across.z) > 0.0f);
    for (const double side : {-1.0, 1.0}) {
        const Point pillar = at(arch, side * arch.halfSpan, 0.0);
        for (double y = arch.base - p.anchorDepth + 0.5; y < spring + 0.5 * arch.halfSpan;
             y += 1.0) {
            TEST_CHECK(field.rock(arch, pillar.x, y, pillar.z) < 0.0f);
        }
        TEST_CHECK(field.rock(arch, pillar.x, arch.base - p.anchorDepth - 0.5, pillar.z) > 0.0f);
        // No seam where the half-torus rests on the pillar.
        TEST_CHECK(field.rock(arch, pillar.x, spring, pillar.z) < 0.0f);
        const Point beside = at(arch, side * arch.halfSpan, p.pillarRadius + 0.5);
        TEST_CHECK(field.rock(arch, beside.x, arch.base + 1.0, beside.z) > 0.0f);
    }
}

// The opening: everything under the arch, from the base up to the top of the opening and
// across the tube, is carved; nothing outside it.
void testOpening() {
    const ArchField field(kSeed);
    const ArchParams& p = field.params();
    const Arch arch = makeArch();
    for (double y = arch.base + 0.5; y < field.openingTop(arch); y += 1.0) {
        for (const double across : {-0.9 * p.tubeRadius, 0.0, 0.9 * p.tubeRadius}) {
            const Point under = at(arch, 0.0, across);
            TEST_CHECK(field.opening(arch, under.x, y, under.z) < 0.0f);
            // Carved out of solid ground, under the arch it is air.
            TEST_CHECK(field.apply(arch, -50.0f, under.x, y, under.z) > 0.0f);
        }
    }
    const Point center = at(arch, 0.0, 0.0);
    TEST_CHECK(field.opening(arch, center.x, arch.base - 0.5, center.z) > 0.0f); // the floor
    TEST_CHECK(field.opening(arch, center.x, field.springline(arch), center.z) < 0.0f); // no seam
    const Point outside = at(arch, 0.0, p.openingHalfDepth + 0.5);
    TEST_CHECK(field.opening(arch, outside.x, arch.base + 2.0, outside.z) > 0.0f);
    TEST_CHECK(field.apply(arch, -50.0f, outside.x, arch.base + 2.0, outside.z) < 0.0f);
    // The arch stands in the air: its rock is kept whatever the terrain.
    TEST_CHECK(field.apply(arch, 50.0f, center.x, field.springline(arch) + arch.halfSpan,
                           center.z) < 0.0f);
}

// Rock and opening never overlap, both lie within the bounding radius, and at the reach the
// arch is at least reachMargin away.
void testExtent() {
    const ArchField field(kSeed);
    const ArchParams& p = field.params();
    const Arch arch = makeArch();
    const double radius = field.boundingRadius(arch);
    const double reach = field.reach(arch);
    bool disjoint = true;
    bool bounded = true;
    bool farAtReach = true;
    for (double along = -reach; along <= reach; along += 1.5) {
        for (double across = -reach; across <= reach; across += 1.5) {
            const Point point = at(arch, along, across);
            const double distance = std::hypot(along, across);
            for (double y = arch.base - p.anchorDepth - 2.0; y <= field.crown(arch) + 2.0;
                 y += 1.5) {
                const float rock = field.rock(arch, point.x, y, point.z);
                const float opening = field.opening(arch, point.x, y, point.z);
                disjoint = disjoint && !(rock < 0.0f && opening < 0.0f);
                bounded = bounded && (distance <= radius || (rock >= 0.0f && opening >= 0.0f));
                farAtReach = farAtReach && (distance < reach + 0.01 || rock >= p.reachMargin);
            }
        }
    }
    TEST_CHECK(disjoint);
    TEST_CHECK(bounded);
    TEST_CHECK(farAtReach);
}

// The direction of the span only rotates the arch.
void testRotation() {
    const ArchField field(kSeed);
    Arch a = makeArch();
    Arch b = makeArch();
    b.directionX = -a.directionZ;
    b.directionZ = a.directionX;
    bool rotated = true;
    for (const double along : {-27.0, -10.0, 0.0, 4.0, 25.0}) {
        for (const double across : {-5.5, 0.0, 3.0}) {
            const Point pa = at(a, along, across);
            const Point pb = at(b, along, across);
            for (const double y : {95.0, 101.0, 120.0, 130.0}) {
                rotated = rotated && std::abs(field.rock(a, pa.x, y, pa.z) -
                                              field.rock(b, pb.x, y, pb.z)) < 1.0e-3f;
            }
        }
    }
    TEST_CHECK(rotated);
}

} // namespace

int main() {
    testCandidates();
    testHeights();
    testRock();
    testOpening();
    testExtent();
    testRotation();
    return test::result();
}
