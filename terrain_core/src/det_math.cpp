#include "terrain/det_math.hpp"

#include <array>
#include <cstddef>
#include <limits>

namespace terrain::dmath {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr double kPi = 0x1.921fb54442d18p+1;
constexpr double kHalfPi = 0x1.921fb54442d18p+0;
constexpr double kQuarterPi = 0x1.921fb54442d18p-1;
constexpr double kThreeQuarterPi = 0x1.2d97c7f3321d2p+1;
constexpr double kSixthPi = 0x1.0c152382d7366p-1;
constexpr double kTwoPi = 0x1.921fb54442d18p+2;
constexpr double kTwoOverPi = 0x1.45f306dc9c883p-1;
// pi / 2 in three parts of 33 bits (as in fdlibm): n * part is exact for |n| < 2^20.
constexpr double kHalfPi1 = 0x1.921fb544p+0;
constexpr double kHalfPi2 = 0x1.0b4611a6p-34;
constexpr double kHalfPi3 = 0x1.3198a2ep-69;
constexpr double kLog2E = 0x1.71547652b82fep+0;
// ln 2 in two parts (as in fdlibm): n * kLn2Hi is exact for |n| < 2^11.
constexpr double kLn2Hi = 0x1.62e42feep-1;
constexpr double kLn2Lo = 0x1.a39ef35793c76p-33;
constexpr double kSqrt3 = 0x1.bb67ae8584caap+0;
constexpr double kTanTwelfthPi = 0x1.126145e9ecd56p-2; // tan(pi / 12) = 2 - sqrt(3)
constexpr double kSqrtHalf = 0x1.6a09e667f3bcdp-1;
constexpr double kExpOverflow = 0x1.62e42fefa39efp+9; // 1024 ln 2
constexpr double kExpUnderflow = -745.2;              // below the smallest subnormal

// sin(r) = r + r^3 (-1/3! + r^2 / 5! - ... + r^14 / 17!) for |r| <= pi / 4: error below 1e-19.
constexpr std::array<double, 8> kSinTail = {
    -0x1.5555555555555p-3,  0x1.1111111111111p-7,  -0x1.a01a01a01a01ap-13, 0x1.71de3a556c734p-19,
    -0x1.ae64567f544e4p-26, 0x1.6124613a86d09p-33, -0x1.ae7f3e733b81fp-41, 0x1.952c77030ad4ap-49,
};
// cos(r) = 1 + r^2 (-1/2! + r^2 / 4! - ... - r^16 / 18!) for |r| <= pi / 4: error below 1e-20.
constexpr std::array<double, 9> kCosTail = {
    -0x1p-1,
    0x1.5555555555555p-5,
    -0x1.6c16c16c16c17p-10,
    0x1.a01a01a01a01ap-16,
    -0x1.27e4fb7789f5cp-22,
    0x1.1eed8eff8d898p-29,
    -0x1.93974a8c07c9dp-37,
    0x1.ae7f3e733b81fp-45,
    -0x1.6827863b97d97p-53,
};
// e^r = 1 + r + r^2 / 2! + ... + r^13 / 13! for |r| <= ln 2 / 2: error below 5e-18.
constexpr std::array<double, 14> kExp = {
    1.0,
    1.0,
    0x1p-1,
    0x1.5555555555555p-3,
    0x1.5555555555555p-5,
    0x1.1111111111111p-7,
    0x1.6c16c16c16c17p-10,
    0x1.a01a01a01a01ap-13,
    0x1.a01a01a01a01ap-16,
    0x1.71de3a556c734p-19,
    0x1.27e4fb7789f5cp-22,
    0x1.ae64567f544e4p-26,
    0x1.1eed8eff8d898p-29,
    0x1.6124613a86d09p-33,
};
// 2^f = sum of (f ln 2)^k / k! for k = 0..13 and |f| <= 1/2: error below 5e-18.
constexpr std::array<double, 14> kExp2 = {
    1.0,
    0x1.62e42fefa39efp-1,
    0x1.ebfbdff82c58fp-3,
    0x1.c6b08d704a0c0p-5,
    0x1.3b2ab6fba4e77p-7,
    0x1.5d87fe78a6731p-10,
    0x1.430912f86c787p-13,
    0x1.ffcbfc588b0c7p-17,
    0x1.62c0223a5c824p-20,
    0x1.b5253d395e7c4p-24,
    0x1.e4cf5158b8ecap-28,
    0x1.e8cac7351bb25p-32,
    0x1.c3bd650fc2986p-36,
    0x1.816193166d0f9p-40,
};
// e^x - 1 = x + x^2 (1/2! + x / 3! + ... + x^15 / 17!) for |x| < 1/2: error below 1e-19.
constexpr std::array<double, 16> kExpm1Tail = {
    0x1p-1,
    0x1.5555555555555p-3,
    0x1.5555555555555p-5,
    0x1.1111111111111p-7,
    0x1.6c16c16c16c17p-10,
    0x1.a01a01a01a01ap-13,
    0x1.a01a01a01a01ap-16,
    0x1.71de3a556c734p-19,
    0x1.27e4fb7789f5cp-22,
    0x1.ae64567f544e4p-26,
    0x1.1eed8eff8d898p-29,
    0x1.6124613a86d09p-33,
    0x1.93974a8c07c9dp-37,
    0x1.ae7f3e733b81fp-41,
    0x1.ae7f3e733b81fp-45,
    0x1.952c77030ad4ap-49,
};
// atanh(s) = s + s^3 (1/3 + s^2 / 5 + ... + s^20 / 23) for |s| <= 0.1716: error below 1e-18.
constexpr std::array<double, 11> kAtanhTail = {
    0x1.5555555555555p-2, 0x1.999999999999ap-3, 0x1.2492492492492p-3, 0x1.c71c71c71c71cp-4,
    0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4, 0x1.1111111111111p-4, 0x1.e1e1e1e1e1e1ep-5,
    0x1.af286bca1af28p-5, 0x1.8618618618618p-5, 0x1.642c8590b2164p-5,
};
// atan(u) = u + u^3 (-1/3 + u^2 / 5 - ... + u^26 / 29) for |u| <= 0.268: error below 1e-18.
constexpr std::array<double, 14> kAtanTail = {
    -0x1.5555555555555p-2, 0x1.999999999999ap-3, -0x1.2492492492492p-3, 0x1.c71c71c71c71cp-4,
    -0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4, -0x1.1111111111111p-4, 0x1.e1e1e1e1e1e1ep-5,
    -0x1.af286bca1af28p-5, 0x1.8618618618618p-5, -0x1.642c8590b2164p-5, 0x1.47ae147ae147bp-5,
    -0x1.2f684bda12f68p-5, 0x1.1a7b9611a7b96p-5,
};

// c[0] + c[1] x + c[2] x^2 + ... by Horner's rule.
template <std::size_t N>
double horner(const std::array<double, N>& c, double x) {
    double result = c[N - 1];
    for (std::size_t i = N - 1; i-- > 0;) {
        result = result * x + c[i];
    }
    return result;
}

double sinKernel(double r) {
    const double r2 = r * r;
    return r + r * r2 * horner(kSinTail, r2);
}

double cosKernel(double r) {
    const double r2 = r * r;
    return 1.0 + r2 * horner(kCosTail, r2);
}

// Nearest integer to x, ties to even: exact, odd (roundHalfEven(-x) = -roundHalfEven(x)) and
// unchanged by a shift of x by an even integer, unlike std::round at the ties.
double roundHalfEven(double x) {
    const double down = std::floor(x);
    const double rest = x - down;
    if (rest != 0.5) {
        return rest < 0.5 ? down : down + 1.0;
    }
    return down - 2.0 * std::floor(0.5 * down) == 0.0 ? down : down + 1.0;
}

// n modulo 4 for an integer n, exact.
double modFour(double n) {
    return n - 4.0 * std::floor(0.25 * n);
}

// sin(quarter * pi / 2 + r) for quarter in {0, 1, 2, 3} and |r| <= pi / 4.
double sinQuadrant(double quarter, double r) {
    switch (static_cast<int>(quarter)) {
    case 0:
        return sinKernel(r);
    case 1:
        return cosKernel(r);
    case 2:
        return -sinKernel(r);
    default:
        return -cosKernel(r);
    }
}

// x - n pi / 2 for the nearest integer n, with pi / 2 in three parts.
double reduceHalfPi(double x, double& n) {
    n = std::round(x * kTwoOverPi);
    return ((x - n * kHalfPi1) - n * kHalfPi2) - n * kHalfPi3;
}

// ln(m) = 2 atanh((m - 1) / (m + 1)) for m in [sqrt(1/2), sqrt(2)].
double logMantissa(double m) {
    const double s = (m - 1.0) / (m + 1.0);
    const double s2 = s * s;
    return 2.0 * (s + s * s2 * horner(kAtanhTail, s2));
}

// Splits a finite x > 0 into m 2^exponent with m in [sqrt(1/2), sqrt(2)).
double mantissa(double x, int& exponent) {
    double m = std::frexp(x, &exponent);
    if (m < kSqrtHalf) {
        m *= 2.0;
        --exponent;
    }
    return m;
}

// Logarithm of x when it is not a finite positive number.
double logSpecial(double x) {
    if (std::isnan(x) || x < 0.0) {
        return kNaN;
    }
    return x == 0.0 ? -kInfinity : kInfinity;
}

double atanKernel(double u) {
    const double u2 = u * u;
    return u + u * u2 * horner(kAtanTail, u2);
}

// atan(t) for t in [0, 1]: beyond tan(pi / 12), atan(t) = pi / 6 + atan(u) with
// u = (t sqrt(3) - 1) / (t + sqrt(3)) in [-0.268, 0.268].
double atanUnit(double t) {
    if (t <= kTanTwelfthPi) {
        return atanKernel(t);
    }
    return kSixthPi + atanKernel((t * kSqrt3 - 1.0) / (t + kSqrt3));
}

// x^n for an integer 0 < |n| <= 64, by repeated squaring: pow(x, 2) is exactly x * x.
double powInteger(double x, int n) {
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

} // namespace

double sinTurns(double turns) {
    const double quarters = roundHalfEven(4.0 * turns);
    if (!std::isfinite(quarters)) {
        return kNaN;
    }
    // Exact: turns and quarters / 4 are within a factor 2 of each other (Sterbenz).
    const double r = (turns - 0.25 * quarters) * kTwoPi;
    return sinQuadrant(modFour(quarters), r) + 0.0; // + 0.0: no negative zero
}

double cosTurns(double turns) {
    const double quarters = roundHalfEven(4.0 * turns);
    if (!std::isfinite(quarters)) {
        return kNaN;
    }
    const double r = (turns - 0.25 * quarters) * kTwoPi;
    return sinQuadrant(modFour(quarters + 1.0), r) + 0.0;
}

double sin(double x) {
    if (!std::isfinite(x)) {
        return kNaN;
    }
    double n = 0.0;
    const double r = reduceHalfPi(x, n);
    return sinQuadrant(modFour(n), r);
}

double cos(double x) {
    if (!std::isfinite(x)) {
        return kNaN;
    }
    double n = 0.0;
    const double r = reduceHalfPi(x, n);
    return sinQuadrant(modFour(n + 1.0), r);
}

double exp2(double x) {
    if (std::isnan(x)) {
        return x;
    }
    if (x >= 1024.0) {
        return kInfinity;
    }
    if (x < -1080.0) {
        return 0.0;
    }
    const double n = std::round(x);
    return std::ldexp(horner(kExp2, x - n), static_cast<int>(n));
}

double exp(double x) {
    if (std::isnan(x)) {
        return x;
    }
    if (x > kExpOverflow) {
        return kInfinity;
    }
    if (x < kExpUnderflow) {
        return 0.0;
    }
    const double n = std::round(x * kLog2E);
    const double r = (x - n * kLn2Hi) - n * kLn2Lo;
    return std::ldexp(horner(kExp, r), static_cast<int>(n));
}

double expm1(double x) {
    if (std::abs(x) < 0.5) {
        return x + x * x * horner(kExpm1Tail, x);
    }
    return exp(x) - 1.0;
}

double log2(double x) {
    if (!(x > 0.0) || std::isinf(x)) {
        return logSpecial(x);
    }
    int exponent = 0;
    const double m = mantissa(x, exponent);
    return static_cast<double>(exponent) + logMantissa(m) * kLog2E;
}

double log(double x) {
    if (!(x > 0.0) || std::isinf(x)) {
        return logSpecial(x);
    }
    int exponent = 0;
    const double m = mantissa(x, exponent);
    const auto e = static_cast<double>(exponent);
    return e * kLn2Hi + (logMantissa(m) + e * kLn2Lo);
}

double pow(double x, double y) {
    if (y == 0.0 || x == 1.0) {
        return 1.0;
    }
    if (std::isnan(x) || std::isnan(y)) {
        return kNaN;
    }
    if (std::abs(y) <= 64.0 && y == std::floor(y)) {
        return powInteger(x, static_cast<int>(y));
    }
    if (x < 0.0) {
        return kNaN;
    }
    if (x == 0.0) {
        return y > 0.0 ? 0.0 : kInfinity;
    }
    return exp2(y * log2(x));
}

double tanh(double x) {
    const double t = std::abs(x);
    if (!(t >= 0x1p-28)) {
        return x; // tanh(x) = x - x^3 / 3 rounds to x; also NaN
    }
    if (t > 22.0) {
        return x > 0.0 ? 1.0 : -1.0;
    }
    const double e = expm1(-2.0 * t);
    const double result = -e / (2.0 + e);
    return x > 0.0 ? result : -result;
}

double atan(double x) {
    if (std::isnan(x)) {
        return x;
    }
    const double t = std::abs(x);
    const double result = t <= 1.0 ? atanUnit(t) : kHalfPi - atanUnit(1.0 / t);
    return std::signbit(x) ? -result : result;
}

double atan2(double y, double x) {
    if (std::isnan(x) || std::isnan(y)) {
        return kNaN;
    }
    if (std::isinf(x) && std::isinf(y)) {
        const double angle = x > 0.0 ? kQuarterPi : kThreeQuarterPi;
        return y > 0.0 ? angle : -angle;
    }
    if (x == 0.0) {
        if (y == 0.0) {
            const double angle = std::signbit(x) ? kPi : 0.0;
            return std::signbit(y) ? -angle : angle;
        }
        return y > 0.0 ? kHalfPi : -kHalfPi;
    }
    const double base = atan(y / x);
    if (x > 0.0) {
        return base;
    }
    return std::signbit(y) ? base - kPi : base + kPi;
}

} // namespace terrain::dmath
