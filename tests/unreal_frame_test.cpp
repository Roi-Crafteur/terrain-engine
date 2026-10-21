// Frame of the Unreal plugin: meters to centimeters, Y up to Z up, round trips between the core
// and Unreal up to the size of the planet, the shift of the origin rebasing, the reflection of
// the axes that keeps the index order of the triangles, the chunk under an Unreal position, and
// the return of a far player by whole worlds.
#include "unreal_frame.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

using terrain::Vec3f;
using terrain::unreal::CorePoint;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;
using terrain::unreal::UnrealVector3f;

// Within a few rounding errors of a double at the magnitude scale (at least 1).
bool closeTo(double a, double b, double scale) {
    return std::abs(a - b) <= 1e-14 * std::max(1.0, scale);
}

double magnitude(const CorePoint& p) {
    return std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)});
}

double magnitude(const UnrealPoint& p) {
    return std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)});
}

// The scale of the computations of a point in a frame, in meters.
double scaleOf(const UnrealFrame& frame, const CorePoint& point) {
    return std::max(magnitude(frame.origin), magnitude(point));
}

bool closeTo(const CorePoint& a, const CorePoint& b, double scale) {
    return closeTo(a.x, b.x, scale) && closeTo(a.y, b.y, scale) && closeTo(a.z, b.z, scale);
}

bool closeTo(const UnrealPoint& a, const UnrealPoint& b, double scale) {
    return closeTo(a.x, b.x, scale) && closeTo(a.y, b.y, scale) && closeTo(a.z, b.z, scale);
}

Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

UnrealVector3f cross(const UnrealVector3f& a, const UnrealVector3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

UnrealVector3f negated(const UnrealVector3f& v) {
    return {-v.x, -v.y, -v.z};
}

constexpr std::array<CorePoint, 6> kPoints{{
    {0.0, 0.0, 0.0},
    {1.0, 2.0, 3.0},
    {-0.125, 1234.5678, -98765.4321},
    {29999999.75, 8848.86, -17499999.5},
    {-30000000.0, -10994.0, 17500000.0},
    {123.456789, -0.001, 1e-9},
}};

constexpr std::array<UnrealFrame, 3> kFrames{{
    {{0.0, 0.0, 0.0}},
    {{10.0, -5.0, 20.0}},
    {{-29999000.5, 1272.43, 17499000.25}},
}};

// Meters become centimeters; core x, y (up), z become Unreal X, Z (up), Y.
void testMetersAndAxes() {
    TEST_CHECK(terrain::unreal::toUnreal({}, {1.0, 2.0, 3.0}) ==
               (UnrealPoint{100.0, 300.0, 200.0}));
    const UnrealFrame frame{{10.0, -5.0, 20.0}};
    TEST_CHECK(terrain::unreal::toUnreal(frame, {11.0, -3.0, 23.0}) ==
               (UnrealPoint{100.0, 300.0, 200.0}));
    TEST_CHECK(terrain::unreal::toUnreal(frame, frame.origin) == UnrealPoint{});
    TEST_CHECK(terrain::unreal::toCore(frame, {100.0, 300.0, 200.0}) ==
               (CorePoint{11.0, -3.0, 23.0}));
}

// Mesh offsets are scaled, directions only have their axes swapped: up stays a unit +Z.
void testOffsetsAndDirections() {
    TEST_CHECK(terrain::unreal::toUnrealOffset({1.0f, 2.0f, 3.0f}) ==
               (UnrealVector3f{100.0f, 300.0f, 200.0f}));
    TEST_CHECK(terrain::unreal::toUnrealOffset({-0.5f, 0.25f, 16.0f}) ==
               (UnrealVector3f{-50.0f, 1600.0f, 25.0f}));
    TEST_CHECK(terrain::unreal::toUnrealDirection({0.0f, 1.0f, 0.0f}) ==
               (UnrealVector3f{0.0f, 0.0f, 1.0f}));
    TEST_CHECK(terrain::unreal::toUnrealDirection({0.6f, 0.0f, 0.8f}) ==
               (UnrealVector3f{0.6f, 0.8f, 0.0f}));
}

// Core -> Unreal -> core and Unreal -> core -> Unreal, up to the size of the planet.
void testRoundTrips() {
    bool coreTrip = true;
    bool unrealTrip = true;
    for (const UnrealFrame& frame : kFrames) {
        for (const CorePoint& point : kPoints) {
            const double scale = scaleOf(frame, point);
            const UnrealPoint unreal = terrain::unreal::toUnreal(frame, point);
            coreTrip = coreTrip && closeTo(terrain::unreal::toCore(frame, unreal), point, scale);
            const UnrealPoint asUnreal{point.x * 100.0, point.y * 100.0, point.z * 100.0};
            const CorePoint core = terrain::unreal::toCore(frame, asUnreal);
            unrealTrip = unrealTrip &&
                         closeTo(terrain::unreal::toUnreal(frame, core), asUnreal, 100.0 * scale);
        }
    }
    TEST_CHECK(coreTrip);
    TEST_CHECK(unrealTrip);
}

// After Unreal moves the world by an offset, every core point lies at its former Unreal
// position plus the offset; the opposite shift gives the frame back.
void testShift() {
    constexpr std::array<UnrealPoint, 3> kOffsets{{
        {12345.5, -678.25, 90.125},
        {-2.0e9, 3.5e9, -1.0e5},
        {0.0, 0.0, 0.0},
    }};
    bool follows = true;
    bool reversible = true;
    for (const UnrealFrame& frame : kFrames) {
        for (const UnrealPoint& offset : kOffsets) {
            const UnrealFrame moved = terrain::unreal::shifted(frame, offset);
            for (const CorePoint& point : kPoints) {
                const UnrealPoint before = terrain::unreal::toUnreal(frame, point);
                const UnrealPoint expected{before.x + offset.x, before.y + offset.y,
                                           before.z + offset.z};
                const double scale = std::max(100.0 * scaleOf(frame, point), magnitude(offset));
                follows = follows &&
                          closeTo(terrain::unreal::toUnreal(moved, point), expected, 4.0 * scale);
            }
            const UnrealFrame back =
                terrain::unreal::shifted(moved, {-offset.x, -offset.y, -offset.z});
            const double scale = std::max(magnitude(frame.origin), magnitude(offset) / 100.0);
            reversible = reversible && closeTo(back.origin, frame.origin, 4.0 * scale);
        }
    }
    TEST_CHECK(follows);
    TEST_CHECK(reversible);
}

// The swap of Y and Z reverses cross products: D(a) x D(b) = -D(a x b). This is why the
// counter-clockwise triangles of the core keep their index order in Unreal.
void testSwapReversesCrossProducts() {
    constexpr std::array<Vec3f, 4> kVectors{{
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {2.0f, -3.0f, 5.0f}, {-4.0f, 0.5f, 1.5f}}};
    bool reversed = true;
    for (const Vec3f& a : kVectors) {
        for (const Vec3f& b : kVectors) {
            const UnrealVector3f swapped = cross(terrain::unreal::toUnrealDirection(a),
                                                 terrain::unreal::toUnrealDirection(b));
            reversed = reversed && swapped == negated(terrain::unreal::toUnrealDirection(
                                                   cross(a, b)));
        }
    }
    TEST_CHECK(reversed);
    // Core: x (east) x y (up) = z (south). Unreal: X x Z (up) = -Y.
    TEST_CHECK(cross(UnrealVector3f{1.0f, 0.0f, 0.0f}, UnrealVector3f{0.0f, 0.0f, 1.0f}) ==
               (UnrealVector3f{0.0f, -1.0f, 0.0f}));
}

// Chunk under an Unreal position, floor division included.
void testChunkAt() {
    const UnrealFrame frame{{0.0, 50.0, 0.0}};
    TEST_CHECK(terrain::unreal::chunkAt(frame, {3250.0, -1601.0, 0.0}) ==
               (terrain::ChunkCoord{2, -2}));
    TEST_CHECK(terrain::unreal::chunkAt(frame, {0.0, 0.0, -1.0e6}) == (terrain::ChunkCoord{0, 0}));
    const UnrealFrame distant{{1600.0, 0.0, -1600.0}};
    TEST_CHECK(terrain::unreal::chunkAt(distant, {-1.0, 1.0, 0.0}) ==
               (terrain::ChunkCoord{99, -100}));
}

// Within the threshold nothing moves; beyond it, whole periods bring the position back within the
// threshold on the other side (never back and forth), exactly, along X (core x) and Y (core z).
void testWorldTurnOffset() {
    const terrain::WorldSize world;
    const double px = world.x * 100.0;
    const double pz = world.z * 100.0;
    const double limit = terrain::unreal::kWorldTurnThreshold;
    TEST_CHECK(terrain::unreal::worldTurnOffset({0.0, 0.0, 5.0e5}, world) == UnrealPoint{});
    TEST_CHECK(terrain::unreal::worldTurnOffset({limit * px, -limit * pz, 0.0}, world) ==
               UnrealPoint{});
    const UnrealPoint east{limit * px + 1.0, 12.5, 7.0};
    TEST_CHECK(terrain::unreal::worldTurnOffset(east, world) == (UnrealPoint{-px, 0.0, 0.0}));
    const UnrealPoint north{3.0, -(limit * pz + 1.0), 0.0};
    TEST_CHECK(terrain::unreal::worldTurnOffset(north, world) == (UnrealPoint{0.0, pz, 0.0}));
    TEST_CHECK(terrain::unreal::worldTurnOffset({-3.6 * px, 2.9 * pz, 0.0}, world) ==
               (UnrealPoint{3.0 * px, -3.0 * pz, 0.0})); // to -0.6 and -0.1 of a world
    const double nan = std::nan("");
    TEST_CHECK(terrain::unreal::worldTurnOffset({nan, nan, 0.0}, world) == UnrealPoint{});
    // Every position ends within the threshold, and a second offset is then zero.
    bool settled = true;
    bool exact = true;
    for (double turns = -5.0; turns <= 5.0; turns += 0.0625) {
        const UnrealPoint start{turns * px + 0.37, -turns * pz - 1234.5678, 100.0};
        const UnrealPoint offset = terrain::unreal::worldTurnOffset(start, world);
        const UnrealPoint moved{start.x + offset.x, start.y + offset.y, start.z + offset.z};
        settled = settled && std::abs(moved.x) < limit * px && std::abs(moved.y) < limit * pz &&
                  terrain::unreal::worldTurnOffset(moved, world) == UnrealPoint{};
        exact = exact && moved.x - offset.x == start.x && moved.y - offset.y == start.y &&
                std::fmod(offset.x, px) == 0.0 && std::fmod(offset.y, pz) == 0.0;
    }
    TEST_CHECK(settled);
    TEST_CHECK(exact);
}

} // namespace

int main() {
    testMetersAndAxes();
    testOffsetsAndDirections();
    testRoundTrips();
    testShift();
    testSwapReversesCrossProducts();
    testChunkAt();
    testWorldTurnOffset();
    return test::result();
}
