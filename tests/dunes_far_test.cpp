// The big crests far from the origin (agent 34): at +-30,000 km, across the seam of the world (x
// wraps at +-30,015 km, z at +-17,498 km), the waves and the giant waves of the default world
// (forced characters) keep their crests: no step, the same bits one period away, their spacing
// as near the origin. Their phase is computed around local pivots, never as a position times a
// direction, so that it never loses precision.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/world_size.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using dunes_test::real;
using terrain::ErgCharacter;

constexpr double kTan40 = 0.83909963117728;

struct Walk {
    double steepest = 0.0;   // m per m between samples 1 m apart
    std::vector<double> gaps; // between the crests met
    bool periodic = true;    // the same bits one period away
    std::array<double, 2> where{}; // the steepest
};

// Along the line from (x, z) toward (ux, uz), length meters every meter: crests are the highest
// within reach meters on each side, over floor meters.
Walk walk(ErgCharacter c, double x, double z, double ux, double uz, double length, double reach,
          double floor) {
    const terrain::WorldSize w;
    Walk out;
    std::vector<double> h;
    for (double d = 0.0; d <= length; d += 1.0) {
        // On a grid of 2^-10 m: one period away is then exactly representable.
        const double px = std::round((x + d * ux) * 1024.0) / 1024.0;
        const double pz = std::round((z + d * uz) * 1024.0) / 1024.0;
        h.push_back(dunes_test::forcedDunes(real().ergs, px, pz, 0.6f, 0.1f, c));
        if (static_cast<int>(d) % 97 == 0) {
            out.periodic = out.periodic &&
                           dunes_test::forcedDunes(real().ergs, px - w.x, pz + w.z, 0.6f, 0.1f, c) ==
                               h.back();
        }
        if (h.size() > 1 && std::abs(h.back() - h[h.size() - 2]) > out.steepest) {
            out.steepest = std::abs(h.back() - h[h.size() - 2]);
            out.where = {px, pz};
        }
    }
    const auto r = static_cast<std::size_t>(reach);
    double last = -1.0;
    for (std::size_t i = r; i + r < h.size(); ++i) {
        bool top = h[i] > floor;
        for (std::size_t k = i - r; k <= i + r && top; ++k) {
            top = !(h[k] > h[i]) && !(k < i && h[k] == h[i]);
        }
        if (top) {
            if (last >= 0.0) {
                out.gaps.push_back(static_cast<double>(i) - last);
            }
            last = static_cast<double>(i);
        }
    }
    return out;
}

void testFar(ErgCharacter c, double low, double high) {
    const bool giant = c == ErgCharacter::Giants;
    const double length = giant ? 60'000.0 : 24'000.0;
    const double reach = giant ? 1'500.0 : 500.0;
    const double floor = giant ? 240.0 : 90.0;
    const terrain::WorldSize w;
    double steepest = 0.0;
    bool periodic = true;
    std::vector<double> far;
    std::vector<double> near;
    for (const double z : {-5'830'000.0, 3'000'000.0, 5'830'000.0}) {
        for (const double x : {30'000'000.0 - 0.5 * length, -30'000'000.0 - 0.5 * length}) {
            const std::array<double, 2> wind = real().ergs.windAt(x, z);
            for (const std::array<double, 2> u : {wind, {1.0, 0.0}}) {
                const Walk k = walk(c, x, z, u[0], u[1], length, reach, floor);
                if (k.steepest > kTan40) {
                    std::printf("  steep %.2f at %.1f %.1f\n", k.steepest, k.where[0], k.where[1]);
                }
                steepest = std::max(steepest, k.steepest);
                periodic = periodic && k.periodic;
                if (u[0] == wind[0]) {
                    far.insert(far.end(), k.gaps.begin(), k.gaps.end());
                }
            }
        }
        // Across the seam in z too (the polar edge), along the wind.
        const double zs = 0.5 * w.z - 0.5 * length;
        const std::array<double, 2> wz = real().ergs.windAt(1'234'567.0, zs);
        const Walk k = walk(c, 1'234'567.0, zs, wz[0], wz[1], length, reach, floor);
        steepest = std::max(steepest, k.steepest);
        periodic = periodic && k.periodic;
        // Near the origin, the same way.
        const std::array<double, 2> wn = real().ergs.windAt(12'345.0, z);
        const Walk n = walk(c, 12'345.0, z, wn[0], wn[1], length, reach, floor);
        near.insert(near.end(), n.gaps.begin(), n.gaps.end());
    }
    const double medianFar = far.empty() ? 0.0 : dunes_test::quantile(far, 0.5);
    const double medianNear = near.empty() ? 0.0 : dunes_test::quantile(near, 0.5);
    std::printf("%s at +-30,000 km: steepest %.1f deg between samples 1 m apart, %s one period "
                "away; crests %zu, median spacing %.0f m (near the origin %.0f m)\n",
                giant ? "giant waves" : "big waves",
                std::atan(steepest) * dunes_test::kDegrees, periodic ? "the same bits" : "DIFFERENT",
                far.size(), medianFar, medianNear);
    TEST_CHECK(steepest <= kTan40);
    TEST_CHECK(periodic);
    TEST_CHECK(far.size() >= 10);
    TEST_CHECK(medianFar >= low && medianFar <= high);
    TEST_CHECK(medianNear > 0.0 && std::max(medianFar, medianNear) / std::min(medianFar, medianNear) <= 1.5);
}

} // namespace

int main() {
    testFar(ErgCharacter::Waves, 1'000.0, 6'000.0);
    testFar(ErgCharacter::Giants, 6'000.0, 20'000.0);
    return test::result();
}
