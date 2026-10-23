// The inline fast paths of det_math give, bit for bit, what the functions they stand for give:
// powInteger(x, n) is pow(x, n) for every x but NaN and every integer |n| <= 64 (special values,
// subnormals, signed zeros and infinities included); sinCosTurns(t) is (sinTurns(t), cosTurns(t))
// for every t (multiples of an eighth of a turn, angles beyond 2^53 turns, infinities and NaN).
#include "terrain/det_math.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace dmath = terrain::dmath;

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

// Special values, then a deterministic spread over many magnitudes and both signs.
std::vector<double> sampleValues() {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    constexpr double kMin = std::numeric_limits<double>::min();
    constexpr double kTiny = std::numeric_limits<double>::denorm_min();
    std::vector<double> values = {0.0,    -0.0,         kTiny,         -kTiny,         kMin,
                                  -kMin,  0.5,          -0.5,          1.0,            -1.0,
                                  2.0,    -2.0,         1.0 + 0x1p-52, 1.0 - 0x1p-53,  0x1p52,
                                  -0x1p52, 0x1p52 + 1.0, 0x1p53,       -0x1p53 - 2.0, 1e300,
                                  -1e300, kInf,         -kInf,         0x1p-1060,      -0x1p-1070};
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 4'000; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const double unit = static_cast<double>(state >> 11) * 0x1p-53;
        const int exponent = static_cast<int>((state >> 3) % 128) - 64;
        values.push_back((i % 2 == 0 ? 1.0 : -1.0) * std::ldexp(unit, exponent));
    }
    return values;
}

void testPowInteger() {
    bool same = true;
    for (const double x : sampleValues()) {
        for (int n = -64; n <= 64; ++n) {
            same = same && sameBits(dmath::powInteger(x, n), dmath::pow(x, n));
        }
    }
    TEST_CHECK(same);
}

bool sameOrNaN(double a, double b) {
    return sameBits(a, b) || (std::isnan(a) && std::isnan(b));
}

void testSinCosTurns() {
    std::vector<double> turns = sampleValues();
    for (int k = -64; k <= 64; ++k) {
        turns.push_back(k / 8.0);
        turns.push_back(k / 8.0 + 1e9);
        turns.push_back(std::nextafter(k / 8.0, 1.0));
    }
    turns.push_back(std::numeric_limits<double>::quiet_NaN());
    bool same = true;
    for (const double t : turns) {
        const dmath::SineCosine both = dmath::sinCosTurns(t);
        same = same && sameOrNaN(both.sine, dmath::sinTurns(t)) &&
               sameOrNaN(both.cosine, dmath::cosTurns(t));
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    testPowInteger();
    testSinCosTurns();
    return test::result();
}
