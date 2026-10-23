// The noises tile the world (WorldSize): the lattice of every octave falls exactly on its sides,
// every noise gives the same bits at every image of an exactly representable point, and the
// noises are continuous across the seams, on the default world and on a small one.
#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace {

namespace noise = terrain::noise;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 20261019;
const std::array<WorldSize, 2> kWorlds = {WorldSize{}, WorldSize{65536.0, 3.0 * 65536.0}};
constexpr noise::FbmParams kFbm{.octaves = 6, .frequency = 1.0 / 900.0};

// Rotations of the lattices, (a, b, c) with a^2 + b^2 = c^2, in the order of the octaves.
constexpr std::array<std::array<std::int64_t, 3>, noise::kRotationCount> kTriples = {{
    {1, 0, 1},
    {4, 3, 5},
    {5, 12, 13},
    {24, 7, 25},
    {3, 4, 5},
    {15, 8, 17},
    {7, 24, 25},
    {12, 5, 13},
}};

// Deterministic coordinates in [-range, range).
class Points {
public:
    double next(double range) {
        state_ = noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) * 0x1p-53;
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_ = 7;
};

// A point of the grid of 1/64 m within 1.5 periods of the origin: images stay exact.
double gridPoint(Points& points, double period) {
    return std::floor(points.next(0.75 * period) * 64.0) / 64.0;
}

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// Every lattice: c * max(1, round(side * frequency / c)) cells per side.
void testLatticeCells() {
    for (const WorldSize& world : kWorlds) {
        for (const double frequency :
             {1.0 / 60000.0, 1.0 / 24000.0, 1.0 / 3000.0, 1.0 / 128.0, 0.37, 1.0e-9}) {
            for (int rotation = 0; rotation < 2 * noise::kRotationCount; ++rotation) {
                const noise::Lattice2D l = noise::lattice2D(world, frequency, rotation);
                const auto& t =
                    kTriples[static_cast<std::size_t>(rotation % noise::kRotationCount)];
                TEST_CHECK(l.a == t[0] && l.b == t[1] && l.c == t[2]);
                TEST_CHECK(l.a * l.a + l.b * l.b == l.c * l.c);
                const auto c = static_cast<double>(l.c);
                const auto expectedX = static_cast<std::int64_t>(
                    c * std::max(1.0, std::round(world.x * frequency / c)));
                const auto expectedZ = static_cast<std::int64_t>(
                    c * std::max(1.0, std::round(world.z * frequency / c)));
                TEST_CHECK(l.cellsX() == expectedX && l.cellsZ() == expectedZ);
                TEST_CHECK(l.scaleX == static_cast<double>(l.cellsX()) / world.x);
                TEST_CHECK(l.scaleZ == static_cast<double>(l.cellsZ()) / world.z);
                TEST_CHECK(l.periodS == l.cellsX() * l.c && l.periodT == l.cellsZ() * l.c);
                // The frequency moves by half a super-cell per period at most.
                if (l.cellsX() > l.c) {
                    TEST_CHECK(std::abs(l.scaleX - frequency) <= 0.5 * c / world.x * 1.000001);
                }
            }
        }
        // A wavelength dividing the world keeps its frequency exactly.
        const noise::Lattice2D exact = noise::lattice2D(world, 1.0 / 128.0);
        TEST_CHECK(exact.scaleX == 1.0 / 128.0 && exact.scaleZ == 1.0 / 128.0);
    }
}

// The noises of one world at (x, y, z): every rotated lattice, fbm, ridged and the 3D ones,
// with each kind of octave lattices.
std::array<float, 15> values(const WorldSize& world, double x, double y, double z) {
    std::array<float, 15> v{};
    for (int r = 0; r < noise::kRotationCount; ++r) {
        const noise::Lattice2D l =
            noise::lattice2D(world, (1.0 + 0.3 * r) / 700.0, r, 0.37 * r, 0.61 * r);
        v[static_cast<std::size_t>(r)] = noise::gradient2D(x, z, kSeed, l);
    }
    v[8] = noise::fbm2D(x, z, kSeed, kFbm, world);
    v[9] = noise::ridged2D(x, z, kSeed, kFbm, world, 4.0);
    v[10] = noise::gradient3D(x, y, z, kSeed, noise::lattice2D(world, 1.0 / 50.0, 3), 1.0 / 50.0);
    v[11] = noise::fbm3D(x, y, z, kSeed, kFbm, world);
    // The lattices of phase 11R: rotated with 1 / c, unrotated with offsets.
    noise::FbmParams fast = kFbm;
    fast.lattices = noise::OctaveLattices::RotatedFast;
    noise::FbmParams unrotated = kFbm;
    unrotated.lattices = noise::OctaveLattices::Unrotated;
    v[12] = noise::fbm2D(x, z, kSeed, fast, world);
    v[13] = noise::ridged2D(x, z, kSeed, fast, world, 4.0);
    v[14] = noise::fbm2D(x, z, kSeed, unrotated, world);
    return v;
}

// value(x) == value(x + k world.x) and value(z) == value(z + l world.z), bit for bit.
void testBitExactImages() {
    for (const WorldSize& world : kWorlds) {
        Points points;
        bool exact = true;
        for (int i = 0; i < 1500; ++i) {
            const double x = gridPoint(points, world.x);
            const double z = gridPoint(points, world.z);
            const double y = std::floor(points.next(200.0) * 64.0) / 64.0;
            const std::array<float, 15> v = values(world, x, y, z);
            for (const auto& [kx, kz] :
                 {std::array<double, 2>{1.0, 0.0}, {0.0, 1.0}, {-2.0, 3.0}}) {
                const std::array<float, 15> image =
                    values(world, x + kx * world.x, y, z + kz * world.z);
                for (std::size_t n = 0; n < v.size(); ++n) {
                    exact = exact && same(v[n], image[n]);
                }
            }
        }
        TEST_CHECK(exact);
    }
}

// Across the seams the noises join continuously: a step of 2 mm across the edge changes
// them as little as anywhere else.
void testSeamContinuity() {
    constexpr double kStep = 1.0e-3;
    for (const WorldSize& world : kWorlds) {
        Points points;
        float worstX = 0.0f;
        float worstZ = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            const double along = points.next(0.5 * world.z);
            const double across = points.next(0.5 * world.x);
            const double y = points.next(100.0);
            const std::array<float, 15> east = values(world, 0.5 * world.x - kStep, y, along);
            const std::array<float, 15> west = values(world, -0.5 * world.x + kStep, y, along);
            const std::array<float, 15> south = values(world, across, y, 0.5 * world.z - kStep);
            const std::array<float, 15> north = values(world, across, y, -0.5 * world.z + kStep);
            for (std::size_t n = 0; n < east.size(); ++n) {
                worstX = std::max(worstX, std::abs(east[n] - west[n]));
                worstZ = std::max(worstZ, std::abs(south[n] - north[n]));
            }
        }
        TEST_CHECK(worstX < 1.0e-3f && worstZ < 1.0e-3f);
    }
}

} // namespace

int main() {
    testLatticeCells();
    testBitExactImages();
    testSeamContinuity();
    return test::result();
}
