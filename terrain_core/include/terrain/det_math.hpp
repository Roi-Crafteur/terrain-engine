#pragma once

// Deterministic math: the same bits on every platform, compiler and standard library. Every
// client generates the world from the seed, so nothing that shapes it may call the
// transcendental functions of the standard library (std::sin, std::pow, std::exp...), whose
// results differ between libm, libc++, the MSVC runtime and constant folding. These functions
// only use +, -, *, / and sqrt, which IEEE 754 rounds correctly, and floor, round, frexp and
// ldexp, which are exact; their polynomial coefficients are hexadecimal literals, exact too.
// They also rely on the floating-point options of terrain_core (no contraction into fused
// multiply-adds, no fast math): see terrain_core/CMakeLists.txt.

// The sources of terrain_core are also compiled by other build systems (the Unreal plugin):
// refuse options that would change their results.
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || defined(_M_FP_CONTRACT)
#error "terrain_core needs strict floating point: no fast math and no floating-point contraction"
#endif

#include <cmath>

namespace terrain::dmath {

// Sine and cosine of an angle in turns (1 turn = 2 pi radians). The reduction to the nearest
// quarter turn is exact, so these are the functions to use for angles drawn as a fraction of a
// turn. Accurate to about 1 ulp; exactly 0 or +-1 on multiples of a quarter turn.
[[nodiscard]] double sinTurns(double turns);
[[nodiscard]] double cosTurns(double turns);

// sinTurns(turns) and cosTurns(turns), bit for bit, the reduction done once.
struct SineCosine {
    double sine;
    double cosine;
};
[[nodiscard]] SineCosine sinCosTurns(double turns);

// Sine and cosine in radians, reduced by three-part pi / 2: accurate to about 1 ulp for
// |x| < 1e5, less accurate (but still deterministic) beyond.
[[nodiscard]] double sin(double x);
[[nodiscard]] double cos(double x);

// 2^x, e^x and e^x - 1 (accurate for small x too). Accurate to about 1 ulp.
[[nodiscard]] double exp2(double x);
[[nodiscard]] double exp(double x);
[[nodiscard]] double expm1(double x);

// Logarithms in base 2 and e. NaN below 0, -infinity at 0. Accurate to about 1 ulp.
[[nodiscard]] double log2(double x);
[[nodiscard]] double log(double x);

// x^y. Integer exponents up to 64 in magnitude use repeated multiplication (pow(x, 2) is
// exactly x * x); other exponents need x >= 0 and use exp2(y * log2(x)), accurate to about
// |y * log2(x)| * 2e-16 relative.
[[nodiscard]] double pow(double x, double y);

// x^n for an integer |n| <= 64, by repeated squaring (pow(x, 2) is exactly x * x): the bits of
// pow(x, n) for every x but NaN. Inline, for the loops that raise many values to one exponent.
[[nodiscard]] inline double powInteger(double x, int n) {
    auto k = static_cast<unsigned>(n < 0 ? -n : n);
    double result = 1.0;
    double base = x;
    while (true) {
        if ((k & 1U) != 0U) {
            result *= base;
        }
        k >>= 1U;
        if (k == 0U) {
            break;
        }
        base *= base;
    }
    return n < 0 ? 1.0 / result : result;
}

// Hyperbolic tangent. Accurate to about 2 ulp.
[[nodiscard]] double tanh(double x);

// Arc tangent in (-pi / 2, pi / 2), and the angle of the point (x, y) in [-pi, pi] with the
// signs of zero handled like std::atan2. Accurate to about 2 ulp.
[[nodiscard]] double atan(double x);
[[nodiscard]] double atan2(double y, double x);

// Euclidean norms, without the overflow protection of std::hypot (useless at the scale of the
// world, and implemented differently by each standard library).
[[nodiscard]] inline double hypot(double x, double y) {
    return std::sqrt(x * x + y * y);
}
[[nodiscard]] inline double hypot(double x, double y, double z) {
    return std::sqrt(x * x + y * y + z * z);
}

} // namespace terrain::dmath
