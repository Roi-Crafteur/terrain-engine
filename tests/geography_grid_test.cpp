// The grid primitives of the planetary geography (internal): B-spline weights and derivatives,
// the spans of coordinates, the exact distance transform on the torus against brute force (with
// initial values, across the seams, on grids of one or a few nodes), connected components,
// box blur and quantile.
#include "geography_grid.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace geo = terrain::geography;

geo::Grid grid(int nx, int nz, double sizeX, double sizeZ) {
    geo::Grid g;
    g.nx = nx;
    g.nz = nz;
    g.sizeX = sizeX;
    g.sizeZ = sizeZ;
    return g;
}

// Deterministic pseudo-random numbers in [0, 1).
class Random {
public:
    double next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state_ >> 11) * 0x1p-53;
    }

private:
    std::uint64_t state_ = 12345;
};

void testSpline() {
    for (const double t : {0.0, 0.1, 0.25, 0.5, 0.77, 0.999}) {
        const geo::Spline s = geo::bspline(t);
        double sum = 0.0;
        double linear = 0.0;
        double dsum = 0.0;
        double dlinear = 0.0;
        bool positive = true;
        for (int k = 0; k < 4; ++k) {
            const auto w = s.w[static_cast<std::size_t>(k)];
            const auto d = s.dw[static_cast<std::size_t>(k)];
            positive = positive && w >= 0.0;
            sum += w;
            linear += w * (k - 1);
            dsum += d;
            dlinear += d * (k - 1);
        }
        TEST_CHECK(positive);
        TEST_CHECK(std::abs(sum - 1.0) < 1e-15);
        TEST_CHECK(std::abs(linear - t) < 1e-15); // reproduces linear functions
        TEST_CHECK(std::abs(dsum) < 1e-15 && std::abs(dlinear - 1.0) < 1e-15);
        // The derivative matches finite differences of the weights.
        const geo::Spline a = geo::bspline(std::max(0.0, t - 1e-6));
        const geo::Spline b = geo::bspline(std::min(1.0, t + 1e-6));
        const double h = std::min(1.0, t + 1e-6) - std::max(0.0, t - 1e-6);
        for (std::size_t k = 0; k < 4; ++k) {
            TEST_CHECK(std::abs((b.w[k] - a.w[k]) / h - s.dw[k]) < 1e-6);
        }
    }
    // C1 across the nodes: the weights at t = 1 of node i are those at t = 0 of node i + 1.
    const geo::Spline end = geo::bspline(1.0);
    const geo::Spline start = geo::bspline(0.0);
    for (std::size_t k = 0; k < 3; ++k) {
        TEST_CHECK(std::abs(end.w[k + 1] - start.w[k]) < 1e-15);
        TEST_CHECK(std::abs(end.dw[k + 1] - start.dw[k]) < 1e-15);
    }
}

void testSpans() {
    const geo::Span a = geo::spanOf(-50.0, 100.0, 10.0);
    TEST_CHECK(a.node == 0 && a.t == 0.0);
    const geo::Span b = geo::spanOf(-12.5, 100.0, 10.0);
    TEST_CHECK(b.node == 3 && std::abs(b.t - 0.75) < 1e-12);
    const geo::Span c = geo::spanOf(49.999, 100.0, 10.0);
    TEST_CHECK(c.node == 9 && c.t > 0.99);
}

double torusOffset(double d, double period) {
    d = std::fmod(std::abs(d), period);
    return std::min(d, period - d);
}

// Brute force against the transform: random sources with random initial values.
void checkTransform(const geo::Grid& g, double density, Random& random) {
    std::vector<double> seed(g.nodes(), geo::kNoSource);
    for (double& s : seed) {
        if (random.next() < density) {
            s = random.next() * 4.0 * g.sizeX * g.sizeX;
        }
    }
    const std::vector<double> fast = geo::distanceTransform(g, seed);
    bool exact = true;
    for (std::size_t p = 0; p < g.nodes(); ++p) {
        double best = geo::kNoSource;
        for (std::size_t q = 0; q < g.nodes(); ++q) {
            if (seed[q] == geo::kNoSource) {
                continue;
            }
            const double dx = torusOffset((g.column(p) - g.column(q)) * g.sizeX, g.periodX());
            const double dz = torusOffset((g.row(p) - g.row(q)) * g.sizeZ, g.periodZ());
            best = std::min(best, dx * dx + dz * dz + seed[q]);
        }
        exact = exact && (best == geo::kNoSource ? fast[p] == geo::kNoSource
                                                 : std::abs(fast[p] - best) <= 1e-9 * best + 1e-6);
    }
    TEST_CHECK(exact);
}

void testDistanceTransform() {
    Random random;
    for (const geo::Grid& g : {grid(1, 1, 10.0, 10.0), grid(1, 3, 10.0, 7.0),
                               grid(5, 7, 10.0, 10.0), grid(13, 9, 3.0, 4.5),
                               grid(40, 24, 65536.0, 65536.0)}) {
        for (const double density : {0.0, 0.02, 0.2, 0.9}) {
            checkTransform(g, density, random);
        }
    }
}

void testComponents() {
    // Two bands joined across the east-west seam, a dot alone, and a diagonal pair.
    const geo::Grid g = grid(8, 6, 1.0, 1.0);
    std::vector<std::uint8_t> member(g.nodes(), 0);
    for (int j = 0; j < 6; ++j) {
        member[g.index(0, j)] = 1;
        member[g.index(7, j)] = 1;
    }
    member[g.index(4, 2)] = 1;
    member[g.index(2, 4)] = 1;
    member[g.index(3, 5)] = 1;
    int eight = 0;
    int four = 0;
    const std::vector<std::int32_t> a = geo::components(g, member, true, eight);
    const std::vector<std::int32_t> b = geo::components(g, member, false, four);
    TEST_CHECK(eight == 3 && four == 4);
    TEST_CHECK(a[g.index(0, 0)] == a[g.index(7, 5)]);
    TEST_CHECK(a[g.index(2, 4)] == a[g.index(3, 5)] && b[g.index(2, 4)] != b[g.index(3, 5)]);
    TEST_CHECK(a[g.index(1, 1)] == -1);
}

void testBlurAndQuantile() {
    const geo::Grid g = grid(9, 5, 1.0, 1.0);
    std::vector<float> constant(g.nodes(), 3.5f);
    geo::boxBlur(g, constant, 2);
    TEST_CHECK(std::all_of(constant.begin(), constant.end(), [](float v) { return v == 3.5f; }));
    std::vector<float> spike(g.nodes(), 0.0f);
    spike[g.index(0, 0)] = 25.0f;
    geo::boxBlur(g, spike, 1);
    double sum = 0.0;
    for (const float v : spike) {
        sum += v;
    }
    TEST_CHECK(std::abs(sum - 25.0) < 1e-4);
    TEST_CHECK(std::abs(spike[g.index(8, 4)] - 25.0f / 9.0f) < 1e-6f); // across both seams
    TEST_CHECK(geo::quantile({5.0f, 1.0f, 4.0f, 2.0f, 3.0f}, 0.0) == 1.0f);
    TEST_CHECK(geo::quantile({5.0f, 1.0f, 4.0f, 2.0f, 3.0f}, 0.5) == 3.0f);
    TEST_CHECK(geo::quantile({5.0f, 1.0f, 4.0f, 2.0f, 3.0f}, 0.74) == 3.0f);
    TEST_CHECK(geo::quantile({5.0f, 1.0f, 4.0f, 2.0f, 3.0f}, 1.0) == 5.0f);
}

void testMakeGrid() {
    const geo::Grid g = geo::makeGrid(terrain::WorldSize{}, 65536.0);
    TEST_CHECK(g.nx == 916 && g.nz == 534 && g.sizeX == 65536.0 && g.sizeZ == 65536.0);
    TEST_CHECK(g.x(0) == -0.5 * terrain::WorldSize{}.x && g.x(916) == 0.5 * terrain::WorldSize{}.x);
    const geo::Grid tiny = geo::makeGrid(terrain::WorldSize{65536.0, 3.0 * 65536.0}, 65536.0 * 4);
    TEST_CHECK(tiny.nx == 1 && tiny.nz == 1);
    TEST_CHECK(tiny.index(-3, 7) == 0);
}

} // namespace

int main() {
    testSpline();
    testSpans();
    testDistanceTransform();
    testComponents();
    testBlurAndQuantile();
    testMakeGrid();
    return test::result();
}
