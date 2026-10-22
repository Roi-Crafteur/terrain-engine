// The walls of the mountains of the geography (mountain_walls.hpp): the closed form of the top of
// the strata against the union of their flat-topped prisms; tops never above the ground but by a
// few hundredths of a band, never below it but by a band, continuous and rising with the ground;
// no wall on the floors and the crests; both characters, strata and crystalline, present (the
// cores of the highest chains crystalline); one
// surface (the density changes sign once along y); the same bits through a memo and from one
// Noises to its twin; continuous across the seams of the world.
#include "mountain_walls.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::WorldSize;
namespace walls = terrain::mountain_walls;

constexpr std::uint64_t kSeed = 0xA11ULL;
constexpr double kTilt = 0.25;
// The strata bands of the walls are at most kBandHigh meters high, their rims recede kRim.
constexpr double kBandHigh = 300.0;
constexpr double kRim = 25.0;

// Uniform in [low, high) from a stream of points.
double uniform(bit_exact::Points& points, double low, double high) {
    return low + (high - low) * 0.5 * (points.next(1.0) + 1.0);
}

// With every layer hard and vertical faces, the top of the strata is the top of the union of
// prisms of tops offset + band j, each standing where the ground, receded by its rim, reaches its
// top: the highest of these levels under the receded ground.
void testStrataClosedForm() {
    bit_exact::Points points;
    bool union_ = true;
    bool soft = true;
    for (int k = 0; k < 20'000; ++k) {
        const double ground = uniform(points, -500.0, 9'000.0);
        const double slope = uniform(points, 0.0, 3.0);
        const double band = uniform(points, 10.0, kBandHigh);
        const double offset = uniform(points, -500.0, 500.0);
        const double rim = uniform(points, 0.0, kRim);
        const auto layers = static_cast<std::uint64_t>(k) * 0x9E3779B97F4A7C15ULL;
        const double top =
            walls::strataTop(ground, slope, band, offset, rim, 0.0, {layers, 2.0, 0.0, 0.0, 0.0});
        const double receded = ground - rim * std::max(slope, 0.05);
        double highest = -1.0e300;
        for (int j = -1'200; j <= 1'200; ++j) {
            const double level = offset + band * j;
            if (level <= receded + 1e-9) {
                highest = std::max(highest, level);
            }
        }
        union_ = union_ && std::abs(top - highest) <= 1e-6 * band;
        // Soft layers keep the receded ground.
        const double kept =
            walls::strataTop(ground, slope, band, offset, rim, 0.0, {layers, -1.0, 0.0, 0.0, 0.0});
        soft = soft && std::abs(kept - receded) <= 1e-9 * std::max(1.0, std::abs(ground));
    }
    TEST_CHECK(union_);
    TEST_CHECK(soft);
}

// Never more than a few hundredths of a band above the ground, at most a band and a rim below
// it; rising with the ground and continuous, the scree at the foot of the walls included; the
// banks thin out and stop with the share of hard layers, without a step.
void testStrataBounds() {
    bit_exact::Points points;
    bool bounded = true;
    bool rising = true;
    bool fading = true;
    for (int k = 0; k < 20'000; ++k) {
        const double ground = uniform(points, 0.0, 9'000.0);
        const double slope = uniform(points, 0.0, 3.0);
        const double band = uniform(points, 10.0, kBandHigh);
        const double offset = uniform(points, -500.0, 500.0);
        const double rim = uniform(points, 0.0, kRim);
        const double run = band * uniform(points, 0.05, 0.25);
        const auto layers = static_cast<std::uint64_t>(k) * 0xD1B54A32D192ED03ULL;
        const double hard = uniform(points, 0.0, 1.0);
        const auto top = [&](double g, double h) {
            return walls::strataTop(g, slope, band, offset, rim, run,
                                    {layers, h, kTilt, 0.7, 0.35});
        };
        const double t = top(ground, hard);
        bounded = bounded && t <= ground + 0.03 * band &&
                  t >= ground - rim * std::max(slope, 0.05) - band - 1e-9;
        // The faces are at most 600 times steeper than the ground below them: no step.
        const double next = top(ground + 1e-3, hard);
        rising = rising && next >= t && next - t <= 600.0 * 1e-3 + 1e-9;
        // The banks thin out with the share of hard layers, without a step.
        fading = fading && std::abs(top(ground, hard + 1e-4) - t) <= 0.002 * band;
    }
    TEST_CHECK(bounded);
    TEST_CHECK(rising);
    TEST_CHECK(fading);
}

// A steep face of a worn core below the crystalline cores of the highest chains (intensity
// 0.65, character 0.4): both characters, region by region.
walls::Column wallColumn(double ground, double gradX, double gradZ) {
    walls::Column c;
    c.ground = ground;
    c.gradX = gradX;
    c.gradZ = gradZ;
    c.intensity = 0.65f;
    c.character = 0.4f;
    c.relief = 1'500.0f;
    return c;
}

// No wall on the floors nor on the crests; both characters on the steep faces of the cores; the
// surface within its bounds, one surface along y.
void testSurface() {
    const walls::Noises n = walls::makeNoises(kSeed, WorldSize{});
    int strata = 0;
    int crystal = 0;
    int total = 0;
    bool flat = true;
    bool bounded = true;
    bool once = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 2e7)) {
        walls::Column c = wallColumn(3'000.0 + 0.001 * p.x, 0.8, -0.6);
        const walls::Masks m = walls::masks(n, c, p.x, p.z, nullptr);
        strata += m.strata > 0.1 && m.strata > m.crystal ? 1 : 0;
        crystal += m.crystal > 0.1 && m.crystal > m.strata ? 1 : 0;
        ++total;
        const double s = walls::surface(n, c, p.x, p.z, nullptr);
        bounded = bounded && s <= c.ground + 0.03 * kBandHigh && s >= c.ground - 800.0;
        once = once && walls::density(n, c, p.x, p.z, s + 0.5, nullptr) > 0.0 &&
               walls::density(n, c, p.x, p.z, s - 0.5, nullptr) < 0.0;
        walls::Column floor = c;
        floor.floor = 1.0f;
        walls::Column crest = c;
        crest.crest = 1.0f;
        flat = flat && walls::surface(n, floor, p.x, p.z, nullptr) == c.ground &&
               walls::surface(n, crest, p.x, p.z, nullptr) == c.ground;
    }
    TEST_CHECK(flat);
    TEST_CHECK(bounded);
    TEST_CHECK(once);
    TEST_CHECK(strata > total / 10 && crystal > total / 10);
    // The cores of the highest chains are crystalline.
    walls::Column high = wallColumn(3'000.0, 0.8, -0.6);
    high.intensity = 1.0f;
    int layered = 0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(2'000, 2e7)) {
        layered += walls::masks(n, high, p.x, p.z, nullptr).strata > 0.5 ? 1 : 0;
    }
    TEST_CHECK(layered < 2'000 / 10);
}

// The same bits through a memo along rows of points and from a twin; another seed differs;
// continuous across the seams of the world.
void testDeterminismAndSeams() {
    const WorldSize world{};
    const walls::Noises a = walls::makeNoises(kSeed, world);
    const walls::Noises b = walls::makeNoises(kSeed, world);
    const walls::Noises c = walls::makeNoises(kSeed + 1, world);
    walls::Memo memo;
    bool same = true;
    int differ = 0;
    double seam = 0.0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(2'000, 1e7)) {
        const walls::Column column = wallColumn(2'000.0, -1.2, 0.4);
        const double s = walls::surface(a, column, p.x, p.z, nullptr);
        same = same && s == walls::surface(b, column, p.x, p.z, nullptr);
        differ += s != walls::surface(c, column, p.x, p.z, nullptr) ? 1 : 0;
        for (int k = 0; k < 8; ++k) {
            const double x = p.x + 3.0 * k;
            same = same && walls::surface(a, column, x, p.z, &memo) ==
                               walls::surface(a, column, x, p.z, nullptr);
        }
        const double east = walls::surface(a, column, 0.5 * world.x - 1e-4, p.z, nullptr);
        const double west = walls::surface(a, column, -0.5 * world.x + 1e-4, p.z, nullptr);
        seam = std::max(seam, std::abs(east - west));
    }
    TEST_CHECK(same);
    TEST_CHECK(differ > 500);
    TEST_CHECK(seam < 0.05); // 0.2 mm apart, faces of slope 100 at most
}

} // namespace

int main() {
    testStrataClosedForm();
    testStrataBounds();
    testSurface();
    testDeterminismAndSeams();
    return test::result();
}
