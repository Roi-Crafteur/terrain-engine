// The great forms of the mountains of the geography (mountain_forms.hpp): summits in about 60 %
// of their cells, on the crests of the massif; their rise within its bounds, none above the
// ceiling, its gradient that of its height, no summit round; valleys that only cut the massif, never below its
// base, with floors and passes under 15 degrees; the same bits through a memo (the summits move
// onto their crests once per cell through it, one by one without it), from a twin, periodic and
// continuous across the seams of the world.
#include "mountain_forms.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::WorldSize;
namespace forms = terrain::mountain_forms;
namespace noise = terrain::noise;

constexpr std::uint64_t kSeed = 0xF0F0ULL;
constexpr double kHeight = 3'800.0; // of the massif
constexpr double kGround = 1'500.0; // regional elevation
// The bounds of the summits: prominence, rise of the massif, ceiling.
constexpr double kProminenceHigh = 2'200.0;
constexpr double kLevel = 0.6;
constexpr double kCeiling = 8'100.0;

forms::Noises noisesOf(std::uint64_t seed, const WorldSize& world) {
    return forms::makeNoises(seed, world,
                             noise::FbmOctaves(noise::deriveSeed(seed, 99),
                                               {.octaves = 1,
                                                .frequency = 1.0 / 14'000.0,
                                                .gain = 0.42,
                                                .lattices = noise::OctaveLattices::RotatedFast},
                                               world));
}

void testSummitsOnCrests() {
    const forms::Noises n = noisesOf(kSeed, WorldSize{});
    int cells = 0;
    int summits = 0;
    int onCrest = 0;
    bool drawn = true;
    for (int j = 0; j < n.cellsZ; j += 7) {
        for (int i = 0; i < n.cellsX; i += 11) {
            const forms::Summit s = forms::summitOfCell(n, i, j);
            ++cells;
            if (!(s.prominence > 0.0)) {
                continue;
            }
            ++summits;
            drawn = drawn && s.prominence >= 1'200.0 && s.prominence <= kProminenceHigh &&
                    s.radius >= 2'500.0 && s.radius <= 3'500.0;
            // 1 - |n| of the first octave of the massif: 1 on its zero lines.
            onCrest += n.crests.ridged2DSlope(s.x, s.z, 1.0, 0.0, 1).head > 0.98f ? 1 : 0;
        }
    }
    TEST_CHECK(drawn);
    TEST_CHECK(summits > cells / 2 && summits < cells * 7 / 10);
    TEST_CHECK(onCrest > summits * 85 / 100); // the others more than 5 km from a crest
}

// The rise: a factor above 0 (the rings lower the massif by up to kMoat each) and up to 1 + the
// highest prominence over kLevel of the massif; nothing where the core is 0 or above the
// ceiling; its gradient that of its height (finite differences over 1 cm, away from the meeting
// of two summits); no summit round: along the 8 directions from its center, the distance at
// which the rise has fallen to half differs by a third or more between the farthest and the
// nearest.
void testRise() {
    const forms::Noises n = noisesOf(kSeed, WorldSize{});
    bool bounded = true;
    bool none = true;
    int raised = 0;
    int matching = 0;
    int compared = 0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 1e6)) {
        const forms::Form r = forms::summits(n, p.x, p.z, kGround, kHeight, 1.0, nullptr);
        // The rings of several summits may overlap: their factors multiply.
        bounded = bounded && r.height > 0.0 &&
                  r.height <= 1.0 + kProminenceHigh / (kLevel * kHeight) + 1e-12;
        raised += r.height > 1.0 ? 1 : 0;
        const forms::Form off = forms::summits(n, p.x, p.z, kGround, kHeight, 0.0, nullptr);
        const forms::Form high =
            forms::summits(n, p.x, p.z, kCeiling - kHeight, kHeight, 1.0, nullptr);
        none = none && off.height == 1.0 && high.height <= 1.0;
        const double step = 0.01;
        const forms::Form x = forms::summits(n, p.x + step, p.z, kGround, kHeight, 1.0, nullptr);
        const forms::Form z = forms::summits(n, p.x, p.z + step, kGround, kHeight, 1.0, nullptr);
        if (r.height != 1.0) {
            ++compared;
            matching += std::abs((x.height - r.height) / step - r.gradX) < 1e-5 &&
                                std::abs((z.height - r.height) / step - r.gradZ) < 1e-5
                            ? 1
                            : 0;
        }
    }
    TEST_CHECK(bounded);
    TEST_CHECK(none);
    TEST_CHECK(raised > 20'000 / 20);
    TEST_CHECK(matching > compared * 99 / 100);
}

void testUneven() {
    const forms::Noises n = noisesOf(kSeed, WorldSize{});
    int summits = 0;
    int uneven = 0;
    for (int j = 0; j < n.cellsZ && summits < 200; j += 13) {
        for (int i = 0; i < n.cellsX && summits < 200; i += 17) {
            const forms::Summit s = forms::summitOfCell(n, i, j);
            if (!(s.prominence > 0.0)) {
                continue;
            }
            const double top = forms::summits(n, s.x, s.z, kGround, kHeight, 1.0, nullptr).height;
            double nearest = 1e30;
            double farthest = 0.0;
            for (int k = 0; k < 8; ++k) {
                const double cx = std::cos(k * 0.785398163397448);
                const double cz = std::sin(k * 0.785398163397448);
                double d = 0.0;
                while (d < 2.0 * s.radius &&
                       forms::summits(n, s.x + d * cx, s.z + d * cz, kGround, kHeight, 1.0,
                                      nullptr)
                               .height > 1.0 + 0.5 * (top - 1.0)) {
                    d += 25.0;
                }
                nearest = std::min(nearest, d);
                farthest = std::max(farthest, d);
            }
            ++summits;
            uneven += farthest > 1.33 * nearest ? 1 : 0;
        }
    }
    TEST_CHECK(summits > 50);
    TEST_CHECK(uneven > summits * 9 / 10);
}

// A massif of constant height cut by the valleys where they are lower (as the Heightfield takes
// them): never below 0, its floors (and the passes they climb to) under 15 degrees, about one
// point in fifty or more on a floor; no step (the ends of the tributaries rise steeply, up to a
// slope of about 13: 1 cm apart, no more than 15 cm).
void testValleys() {
    const forms::Noises n = noisesOf(kSeed, WorldSize{});
    const forms::Form massif{0.8 * kHeight, 0.0, 0.0};
    const double flat = std::tan(15.0 * 3.14159265358979 / 180.0);
    bool cut = true;
    bool gentle = true;
    double jump = 0.0;
    int floors = 0;
    const auto carved = [&](double x, double z) {
        const forms::Carve c = forms::valleys(n, x, z, kHeight, massif, nullptr);
        return c.form.height < massif.height ? c : forms::Carve{massif, 0.0};
    };
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 1e6)) {
        const forms::Carve c = carved(p.x, p.z);
        cut = cut && c.form.height >= 0.0 && c.floor >= 0.0 && c.floor <= 1.0;
        const double x = carved(p.x + 1.0, p.z).form.height;
        const double z = carved(p.x, p.z + 1.0).form.height;
        jump = std::max({jump, std::abs(carved(p.x + 0.01, p.z).form.height - c.form.height),
                         std::abs(carved(p.x, p.z + 0.01).form.height - c.form.height)});
        if (c.floor > 0.9) {
            ++floors;
            gentle = gentle && std::hypot(x - c.form.height, z - c.form.height) < flat;
        }
    }
    TEST_CHECK(cut);
    TEST_CHECK(gentle);
    TEST_CHECK(floors > 20'000 / 50);
    TEST_CHECK(jump < 0.15);
}

// The same bits through the memos along rows of points, from a twin; another seed differs;
// periodic and continuous across the seams of the world.
void testDeterminismAndSeams() {
    const WorldSize world{};
    const forms::Noises a = noisesOf(kSeed, world);
    const forms::Noises b = noisesOf(kSeed, world);
    const forms::Noises c = noisesOf(kSeed + 1, world);
    const forms::Form massif{2'000.0, 0.3, -0.2};
    forms::Memo memo;
    bool same = true;
    int differ = 0;
    double seam = 0.0;
    const auto rise = [](const forms::Form& r) { return r.height * r.height + r.gradX; };
    for (const bit_exact::Point& p : bit_exact::randomPoints(4'000, 1e7)) {
        const forms::Form r = forms::summits(a, p.x, p.z, kGround, kHeight, 1.0, nullptr);
        same = same && rise(r) == rise(forms::summits(b, p.x, p.z, kGround, kHeight, 1.0, nullptr));
        differ += rise(r) != rise(forms::summits(c, p.x, p.z, kGround, kHeight, 1.0, nullptr));
        for (int k = 0; k < 8; ++k) {
            const double x = p.x + 997.0 * k;
            const forms::Form m = forms::summits(a, x, p.z, kGround, kHeight, 1.0, &memo);
            const forms::Form o = forms::summits(a, x, p.z, kGround, kHeight, 1.0, nullptr);
            same = same && rise(m) == rise(o) && m.gradZ == o.gradZ;
            same = same && forms::valleys(a, x, p.z, kHeight, massif, &memo).form.height ==
                               forms::valleys(a, x, p.z, kHeight, massif, nullptr).form.height;
        }
        const double east = forms::summits(a, 0.5 * world.x - 1e-4, p.z, kGround, kHeight, 1.0,
                                           nullptr).height;
        const double west = forms::summits(a, -0.5 * world.x + 1e-4, p.z, kGround, kHeight, 1.0,
                                           nullptr).height;
        const double image = forms::summits(a, p.x + world.x, p.z, kGround, kHeight, 1.0,
                                            nullptr).height;
        seam = std::max({seam, std::abs(east - west), std::abs(image - r.height)});
    }
    TEST_CHECK(same);
    TEST_CHECK(differ > 100);
    TEST_CHECK(seam < 1e-6);
}

} // namespace

int main() {
    testSummitsOnCrests();
    testRise();
    testUneven();
    testValleys();
    testDeterminismAndSeams();
    return test::result();
}
