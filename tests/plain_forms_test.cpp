// The forms of the plains (plain_forms.hpp): nothing at weight 0 (the local relief keeps its
// bits); the kind of plain from flat to rolling from one region to the next; vallons within their
// depth, unevenly spaced, narrow; isolated hills within their height, in a part of the cells,
// rounded on top (no point); bounded and continuous (across the seams of the world too);
// deterministic, the same bits through the memos.
#include "plain_forms.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::WorldSize;
namespace plain_forms = terrain::plain_forms;

constexpr std::uint64_t kSeed = 0x5EED5EEDULL;

// A ground of the plains drawn from a point: full weight, regions and local relief that vary.
plain_forms::Ground groundAt(const bit_exact::Point& p) {
    const double u = std::sin(p.x * 1e-3) * std::cos(p.z * 1.3e-3);
    return {1.0f, static_cast<float>(u), static_cast<float>(30.0 * u)};
}

void testNoWeight() {
    const plain_forms::Noises n = plain_forms::makeNoises(kSeed, WorldSize{});
    bool same = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(5'000, 1e7)) {
        plain_forms::Ground g = groundAt(p);
        g.weight = 0.0f;
        same = same && bit_exact::same(plain_forms::relief(n, p.x, p.z, g, nullptr), g.local);
    }
    TEST_CHECK(same);
}

// From flat to rolling: some almost flat plains, some fully rolling, over the world.
void testKind() {
    const plain_forms::Noises n = plain_forms::makeNoises(kSeed, WorldSize{});
    int flat = 0;
    int rolling = 0;
    bool bounded = true;
    const std::vector<bit_exact::Point> points = bit_exact::randomPoints(4'000, 1e7);
    for (const bit_exact::Point& p : points) {
        const float k = plain_forms::kindAt(n, p.x, p.z, nullptr);
        bounded = bounded && k >= 0.0f && k <= 1.0f;
        flat += k < 0.05f ? 1 : 0;
        rolling += k > 0.95f ? 1 : 0;
    }
    const auto count = static_cast<double>(points.size());
    std::printf("kind of plain: %.1f %% flat, %.1f %% rolling\n", 100.0 * flat / count,
                100.0 * rolling / count);
    TEST_CHECK(bounded);
    TEST_CHECK(flat > 200 && rolling > 200);
}

// Walking 64 km at 10 m: the vallons within their depth, crossed every 1 to 4 km, 30 to 400 m
// wide where deeper than 2 m.
void testVallons() {
    const plain_forms::Noises n = plain_forms::makeNoises(kSeed, WorldSize{});
    bool bounded = true;
    int crossings = 0;
    std::vector<int> widths;
    int run = 0;
    double deepest = 0.0;
    for (int row = 0; row < 4; ++row) {
        for (int k = 0; k < 6'400; ++k) {
            const double d = plain_forms::vallonsAt(n, 1.3e6 * row + 10.0 * k, 2.1e6, nullptr);
            bounded = bounded && d >= 0.0 && d <= 25.0;
            if (d > 2.0) {
                ++run;
                deepest = std::max(deepest, d);
            } else if (run > 0) {
                widths.push_back(10 * run);
                crossings += deepest > 10.0 ? 1 : 0;
                run = 0;
                deepest = 0.0;
            }
        }
    }
    std::sort(widths.begin(), widths.end());
    const int median = widths.empty() ? 0 : widths[widths.size() / 2];
    std::printf("vallons: %d crossed deeper than 10 m over 256 km, median width %d m\n", crossings,
                median);
    TEST_CHECK(bounded);
    TEST_CHECK(crossings >= 64 && crossings <= 256);
    TEST_CHECK(median >= 30 && median <= 400);
}

// The tops of the isolated hills over 140 km (local maxima on a grid of 200 m): within their
// height, one hill in a part of the cells of 7 km, rounded (no point: a top drops by little
// 50 m away).
void testHills() {
    const plain_forms::Noises n = plain_forms::makeNoises(kSeed, WorldSize{});
    constexpr int kSide = 700;
    constexpr double kStep = 200.0;
    std::vector<double> h(static_cast<std::size_t>(kSide) * kSide);
    for (int j = 0; j < kSide; ++j) {
        for (int i = 0; i < kSide; ++i) {
            h[static_cast<std::size_t>(j) * kSide + i] =
                plain_forms::hillAt(n, 3.0e6 + kStep * i, -4.0e6 + kStep * j, nullptr);
        }
    }
    int tops = 0;
    double highest = 0.0;
    double sharpest = 0.0;
    for (int j = 4; j < kSide - 4; ++j) {
        for (int i = 4; i < kSide - 4; ++i) {
            const double here = h[static_cast<std::size_t>(j) * kSide + i];
            bool top = here > 30.0;
            for (int dj = -4; dj <= 4 && top; ++dj) {
                for (int di = -4; di <= 4 && top; ++di) {
                    top = (di == 0 && dj == 0) ||
                          h[static_cast<std::size_t>(j + dj) * kSide + i + di] < here;
                }
            }
            if (!top) {
                continue;
            }
            ++tops;
            highest = std::max(highest, here);
            const double x = 3.0e6 + kStep * i;
            const double z = -4.0e6 + kStep * j;
            double drop = 0.0;
            for (int a = 0; a < 8; ++a) {
                const double angle = 0.785398163397448 * a;
                drop += here - plain_forms::hillAt(n, x + 50.0 * std::cos(angle),
                                                   z + 50.0 * std::sin(angle), nullptr);
            }
            sharpest = std::max(sharpest, drop / 8.0 / here);
        }
    }
    const double cells = (kSide * kStep / 7'000.0) * (kSide * kStep / 7'000.0);
    std::printf("isolated hills: %d tops in %.0f cells, highest %.0f m, sharpest drop at 50 m "
                "%.1f %% of the height\n",
                tops, cells, highest, 100.0 * sharpest);
    TEST_CHECK(tops >= 0.05 * cells && tops <= 0.6 * cells);
    TEST_CHECK(highest >= 100.0 && highest <= 200.0);
    TEST_CHECK(sharpest < 0.05);
}

// Bounded; continuous at 1 cm, across the seams of the world too.
void testBoundsContinuous() {
    const WorldSize world{};
    const plain_forms::Noises n = plain_forms::makeNoises(kSeed, world);
    bool bounded = true;
    double jump = 0.0;
    for (const bit_exact::Point& p : bit_exact::randomPoints(20'000, 0.45 * world.z)) {
        const plain_forms::Ground g = groundAt(p);
        const float r = plain_forms::relief(n, p.x, p.z, g, nullptr);
        bounded = bounded && r > -150.0f && r < 700.0f;
        const float next = plain_forms::relief(n, p.x + 0.01, p.z, g, nullptr);
        const float east = plain_forms::relief(n, 0.5 * world.x - 0.005, p.z, g, nullptr);
        const float west = plain_forms::relief(n, -0.5 * world.x + 0.005, p.z, g, nullptr);
        const float north = plain_forms::relief(n, p.x, 0.5 * world.z - 0.005, g, nullptr);
        const float south = plain_forms::relief(n, p.x, -0.5 * world.z + 0.005, g, nullptr);
        jump = std::max({jump, static_cast<double>(std::abs(next - r)),
                         static_cast<double>(std::abs(east - west)),
                         static_cast<double>(std::abs(north - south))});
    }
    std::printf("steepest step at 1 cm: %.4f m\n", jump);
    TEST_CHECK(bounded);
    TEST_CHECK(jump < 0.05);
}

void testDeterminismAndMemo() {
    const plain_forms::Noises a = plain_forms::makeNoises(kSeed, WorldSize{});
    const plain_forms::Noises b = plain_forms::makeNoises(kSeed, WorldSize{});
    const plain_forms::Noises c = plain_forms::makeNoises(kSeed + 1, WorldSize{});
    plain_forms::Memos memos;
    int differ = 0;
    bool same = true;
    for (const bit_exact::Point& p : bit_exact::randomPoints(2'000, 1e7)) {
        const plain_forms::Ground g = groundAt(p);
        const float r = plain_forms::relief(a, p.x, p.z, g, nullptr);
        same = same && bit_exact::same(r, plain_forms::relief(b, p.x, p.z, g, nullptr));
        differ += r != plain_forms::relief(c, p.x, p.z, g, nullptr) ? 1 : 0;
        // A row of points through the memos: the bits of the calls without them.
        for (int k = 0; k < 8; ++k) {
            const double x = p.x + 37.0 * k;
            same = same && bit_exact::same(plain_forms::relief(a, x, p.z, g, &memos),
                                           plain_forms::relief(a, x, p.z, g, nullptr));
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(differ > 1'900);
}

} // namespace

int main() {
    testNoWeight();
    testKind();
    testVallons();
    testHills();
    testBoundsContinuous();
    testDeterminismAndMemo();
    return test::result();
}
