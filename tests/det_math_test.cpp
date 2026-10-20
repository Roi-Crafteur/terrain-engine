#include "terrain/det_math.hpp"

#include "test_assert.hpp"

#include <array>
#include <bit>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

// The deterministic math functions: accuracy against the standard library, exact bits of
// reference values (the same on every platform), special values and exact identities.

namespace {

namespace dmath = terrain::dmath;

enum class Fn {
    SinTurns,
    CosTurns,
    Sin,
    Cos,
    Exp,
    Exp2,
    Expm1,
    Tanh,
    Atan,
    Log,
    Log2,
    Pow,
    Atan2,
    Hypot
};

double evaluate(Fn fn, double x, double y) {
    switch (fn) {
    case Fn::SinTurns:
        return dmath::sinTurns(x);
    case Fn::CosTurns:
        return dmath::cosTurns(x);
    case Fn::Sin:
        return dmath::sin(x);
    case Fn::Cos:
        return dmath::cos(x);
    case Fn::Exp:
        return dmath::exp(x);
    case Fn::Exp2:
        return dmath::exp2(x);
    case Fn::Expm1:
        return dmath::expm1(x);
    case Fn::Tanh:
        return dmath::tanh(x);
    case Fn::Atan:
        return dmath::atan(x);
    case Fn::Log:
        return dmath::log(x);
    case Fn::Log2:
        return dmath::log2(x);
    case Fn::Pow:
        return dmath::pow(x, y);
    case Fn::Atan2:
        return dmath::atan2(x, y);
    case Fn::Hypot:
        return dmath::hypot(x, y);
    }
    return 0.0;
}

struct Exact {
    Fn fn;
    double x;
    double y;
    std::uint64_t bits;
};

// Captured once (x86-64, GCC and Clang, with and without FMA in the target): every platform
// must give these exact bits.
constexpr Exact kExact[] = {
    {Fn::SinTurns, 0x1.d41d41d41d41ep-7, 0x0p+0, 0x3FB6F29A0C2D213FULL},
    {Fn::CosTurns, 0x1.d41d41d41d41ep-7, 0x0p+0, 0x3FEFDF057B8BB83FULL},
    {Fn::SinTurns, 0x1.b6db6db6db6dbp-5, 0x0p+0, 0x3FD5234ACA69A9FEULL},
    {Fn::CosTurns, 0x1.b6db6db6db6dbp-5, 0x0p+0, 0x3FEE344AD05D3F86ULL},
    {Fn::SinTurns, 0x1.2492492492492p-3, 0x0p+0, 0x3FE904C37505DE4BULL},
    {Fn::CosTurns, 0x1.2492492492492p-3, 0x0p+0, 0x3FE3F3A0E28BEDD1ULL},
    {Fn::SinTurns, 0x1.6db6db6db6db7p-2, 0x0p+0, 0x3FE904C37505DE4BULL},
    {Fn::CosTurns, 0x1.6db6db6db6db7p-2, 0x0p+0, 0xBFE3F3A0E28BEDD1ULL},
    {Fn::SinTurns, -0x1.0ea0ea0ea0ea1p-1, 0x0p+0, 0x3FC6DAF3CBD156A9ULL},
    {Fn::CosTurns, -0x1.0ea0ea0ea0ea1p-1, 0x0p+0, 0xBFEF7C59E7932194ULL},
    {Fn::SinTurns, 0x1.c37fc3c8229f2p+0, 0x0p+0, 0xBFEFE1CF0B670984ULL},
    {Fn::CosTurns, 0x1.c37fc3c8229f2p+0, 0x0p+0, 0x3FB5F55712F041EFULL},
    {Fn::SinTurns, 0x1.c924924924925p+3, 0x0p+0, 0x3FEF329C0558E95FULL},
    {Fn::CosTurns, 0x1.c924924924925p+3, 0x0p+0, 0xBFCC7B90E302462AULL},
    {Fn::Sin, 0x1.999999999999ap-4, 0x0p+0, 0x3FB98EAECB8BCB2CULL},
    {Fn::Cos, 0x1.999999999999ap-4, 0x0p+0, 0x3FEFD712F9A817C0ULL},
    {Fn::Sin, 0x1.8p-2, 0x0p+0, 0x3FD7710255764214ULL},
    {Fn::Cos, 0x1.8p-2, 0x0p+0, 0x3FEDC6B7EB995912ULL},
    {Fn::Sin, 0x1p+0, 0x0p+0, 0x3FEAED548F090CEEULL},
    {Fn::Cos, 0x1p+0, 0x0p+0, 0x3FE14A280FB5068CULL},
    {Fn::Sin, 0x1.4p+1, 0x0p+0, 0x3FE326AF0DCFCAB0ULL},
    {Fn::Cos, 0x1.4p+1, 0x0p+0, 0xBFE9A2F7EF858B7DULL},
    {Fn::Sin, -0x1.d99999999999ap+1, 0x0p+0, 0x3FE0F46AEC2E1B41ULL},
    {Fn::Cos, -0x1.d99999999999ap+1, 0x0p+0, 0xBFEB23A2AD7DD937ULL},
    {Fn::Sin, 0x1.8b0fcb4f1e4b4p+3, 0x0p+0, 0xBFCC05185AF55492ULL},
    {Fn::Cos, 0x1.8b0fcb4f1e4b4p+3, 0x0p+0, 0x3FEF394FD07E24C5ULL},
    {Fn::Sin, 0x1.9p+6, 0x0p+0, 0xBFE03425B78C4DB8ULL},
    {Fn::Cos, 0x1.9p+6, 0x0p+0, 0x3FEB981DBF665FE0ULL},
    {Fn::Exp, 0x1.999999999999ap-4, 0x0p+0, 0x3FF1AEC7B35A00D4ULL},
    {Fn::Exp2, 0x1.999999999999ap-4, 0x0p+0, 0x3FF125FBEE250664ULL},
    {Fn::Expm1, 0x1.47ae147ae147bp-7, 0x0p+0, 0x3F84952E9791133FULL},
    {Fn::Tanh, 0x1.1111111111111p-5, 0x0p+0, 0x3FA10F72FF9A5C30ULL},
    {Fn::Atan, 0x1.999999999999ap-4, 0x0p+0, 0x3FB983E282E2CC4DULL},
    {Fn::Exp, 0x1.8p-2, 0x0p+0, 0x3FF747A513DBEF6AULL},
    {Fn::Exp2, 0x1.8p-2, 0x0p+0, 0x3FF4BFDAD5362A27ULL},
    {Fn::Expm1, 0x1.3333333333333p-5, 0x0p+0, 0x3FA39085DB9004BBULL},
    {Fn::Tanh, 0x1p-3, 0x0p+0, 0x3FBFD5992BC4B834ULL},
    {Fn::Atan, 0x1.8p-2, 0x0p+0, 0x3FD6F61941E4DEF2ULL},
    {Fn::Exp, 0x1p+0, 0x0p+0, 0x4005BF0A8B14576AULL},
    {Fn::Exp2, 0x1p+0, 0x0p+0, 0x4000000000000000ULL},
    {Fn::Expm1, 0x1.999999999999ap-4, 0x0p+0, 0x3FBAEC7B35A00D3AULL},
    {Fn::Tanh, 0x1.5555555555555p-2, 0x0p+0, 0x3FD493AA293C8802ULL},
    {Fn::Atan, 0x1p+0, 0x0p+0, 0x3FE921FB54442D18ULL},
    {Fn::Exp, 0x1.4p+1, 0x0p+0, 0x40285D6FD931E0BBULL},
    {Fn::Exp2, 0x1.4p+1, 0x0p+0, 0x4016A09E667F3BCCULL},
    {Fn::Expm1, 0x1p-2, 0x0p+0, 0x3FD22D78F0FA061AULL},
    {Fn::Tanh, 0x1.aaaaaaaaaaaabp-1, 0x0p+0, 0x3FE5D516AD8C446AULL},
    {Fn::Atan, 0x1.4p+1, 0x0p+0, 0x3FF30B6D796A4DA8ULL},
    {Fn::Exp, -0x1.d99999999999ap+1, 0x0p+0, 0x3F99511FC6871044ULL},
    {Fn::Exp2, -0x1.d99999999999ap+1, 0x0p+0, 0x3FB3B2C47BFF8328ULL},
    {Fn::Expm1, -0x1.7ae147ae147aep-2, 0x0p+0, 0xBFD3CB023BEC0A02ULL},
    {Fn::Tanh, -0x1.3bbbbbbbbbbbcp+0, 0x0p+0, 0xBFEAFE4EE602057DULL},
    {Fn::Atan, -0x1.d99999999999ap+1, 0x0p+0, 0xBFF4E8C94DBF54E4ULL},
    {Fn::Exp, 0x1.8b0fcb4f1e4b4p+3, 0x0p+0, 0x410C125FE69B9015ULL},
    {Fn::Exp2, 0x1.8b0fcb4f1e4b4p+3, 0x0p+0, 0x40B454FBE44282B9ULL},
    {Fn::Expm1, 0x1.3c0ca2a5b1d5dp+0, 0x0p+0, 0x40037EC1A3962BE8ULL},
    {Fn::Tanh, 0x1.075fdcdf69878p+2, 0x0p+0, 0x3FEFFBA2DE509E4BULL},
    {Fn::Atan, 0x1.8b0fcb4f1e4b4p+3, 0x0p+0, 0x3FF7D6EDB0C5B065ULL},
    {Fn::Exp, 0x1.9p+6, 0x0p+0, 0x48F3494A9B171BF5ULL},
    {Fn::Exp2, 0x1.9p+6, 0x0p+0, 0x4630000000000000ULL},
    {Fn::Expm1, 0x1.4p+3, 0x0p+0, 0x40D5825DCF950560ULL},
    {Fn::Tanh, 0x1.0aaaaaaaaaaabp+5, 0x0p+0, 0x3FF0000000000000ULL},
    {Fn::Atan, 0x1.9p+6, 0x0p+0, 0x3FF8F905EB2DEF21ULL},
    {Fn::Log, 0x1.999999999999ap-4, 0x0p+0, 0xC0026BB1BBB55515ULL},
    {Fn::Log2, 0x1.9p+6, 0x0p+0, 0x401A934F0979A371ULL},
    {Fn::Pow, 0x1.999999999999ap-4, 0x1.6p+1, 0x3F5D22A4FA316FACULL},
    {Fn::Pow, 0x1.47ae147ae147bp-8, 0x1p+2, 0x3E05798EE2308C3AULL},
    {Fn::Atan2, 0x1.999999999999ap-4, -0x1.8p+0, 0x400899A67091335FULL},
    {Fn::Hypot, 0x1.999999999999ap-4, 0x1.3333333333333p-2, 0x3FD43D136248490FULL},
    {Fn::Log, 0x1.8p-2, 0x0p+0, 0xBFEF62F40794A7B8ULL},
    {Fn::Log2, 0x1.77p+8, 0x0p+0, 0x402119FB7B8F3242ULL},
    {Fn::Pow, 0x1.8p-2, 0x1.6p+1, 0x3FB1405FEEBC0BE1ULL},
    {Fn::Pow, 0x1.3333333333333p-6, 0x1p+2, 0x3E8096BB98C7E282ULL},
    {Fn::Atan2, 0x1.8p-2, -0x1.8p+0, 0x40072C43F4B1650AULL},
    {Fn::Hypot, 0x1.8p-2, 0x1.3333333333333p-2, 0x3FDEBC28B8599867ULL},
    {Fn::Log, 0x1p+0, 0x0p+0, 0x0000000000000000ULL},
    {Fn::Log2, 0x1.f4p+9, 0x0p+0, 0x4023EE7B471B3A95ULL},
    {Fn::Pow, 0x1p+0, 0x1.6p+1, 0x3FF0000000000000ULL},
    {Fn::Pow, 0x1.999999999999ap-5, 0x1p+2, 0x3EDA36E2EB1C4330ULL},
    {Fn::Atan2, 0x1p+0, -0x1.8p+0, 0x40046DC09EC29432ULL},
    {Fn::Hypot, 0x1p+0, 0x1.3333333333333p-2, 0x3FF0B4597BD9942CULL},
    {Fn::Log, 0x1.4p+1, 0x0p+0, 0x3FED5240F0E0E078ULL},
    {Fn::Log2, 0x1.388p+11, 0x0p+0, 0x4026934F0979A371ULL},
    {Fn::Pow, 0x1.4p+1, 0x1.6p+1, 0x4028DA2A73D8048CULL},
    {Fn::Pow, 0x1p-3, 0x1p+2, 0x3F30000000000000ULL},
    {Fn::Atan2, 0x1.4p+1, -0x1.8p+0, 0x4000E3C51FA059C4ULL},
    {Fn::Hypot, 0x1.4p+1, 0x1.3333333333333p-2, 0x400424BB73DB9E40ULL},
    {Fn::Log, 0x1.d99999999999ap+1, 0x0p+0, 0x3FF4EEEE650AE550ULL},
    {Fn::Log2, 0x1.ce8p+11, 0x0p+0, 0x4027B4E4FD73A56BULL},
    {Fn::Pow, 0x1.d99999999999ap+1, 0x1.6p+1, 0x404242D16ABCDED5ULL},
    {Fn::Pow, 0x1.7ae147ae147aep-3, 0x1p+2, 0x3F5331002818F8CAULL},
    {Fn::Atan2, -0x1.d99999999999ap+1, -0x1.8p+0, 0xBFFF4B963F1E6AACULL},
    {Fn::Hypot, -0x1.d99999999999ap+1, 0x1.3333333333333p-2, 0x400DB277A148F511ULL},
    {Fn::Log, 0x1.8b0fcb4f1e4b4p+3, 0x0p+0, 0x40041B4032C47983ULL},
    {Fn::Log2, 0x1.81cd6c8b43958p+13, 0x0p+0, 0x402B2EF5B86E63D0ULL},
    {Fn::Pow, 0x1.8b0fcb4f1e4b4p+3, 0x1.6p+1, 0x408F5EBFE8AF77B3ULL},
    {Fn::Pow, 0x1.3c0ca2a5b1d5dp-1, 0x1p+2, 0x3FC2959EAC3BF80BULL},
    {Fn::Atan2, 0x1.8b0fcb4f1e4b4p+3, -0x1.8p+0, 0x3FFB1137E7B16F2FULL},
    {Fn::Hypot, 0x1.8b0fcb4f1e4b4p+3, 0x1.3333333333333p-2, 0x4028B2DA64CF4E22ULL},
    {Fn::Log, 0x1.9p+6, 0x0p+0, 0x40126BB1BBB55516ULL},
    {Fn::Log2, 0x1.86ap+16, 0x0p+0, 0x40309C1165EC0627ULL},
    {Fn::Pow, 0x1.9p+6, 0x1.6p+1, 0x41134D0F1066B7CDULL},
    {Fn::Pow, 0x1.4p+2, 0x1p+2, 0x4083880000000000ULL},
    {Fn::Atan2, 0x1.9p+6, -0x1.8p+0, 0x3FF95F6ACA283D6BULL},
    {Fn::Hypot, 0x1.9p+6, 0x1.3333333333333p-2, 0x405900075F6EBBD0ULL},
};

// Distance in units in the last place between two finite doubles.
double ulps(double a, double b) {
    const auto ordered = [](double v) {
        const auto bits = std::bit_cast<std::int64_t>(v);
        return bits < 0 ? std::numeric_limits<std::int64_t>::min() - bits : bits;
    };
    return std::abs(static_cast<double>(ordered(a) - ordered(b)));
}

// Deterministic points in [low, high), or log-uniform in [low, high) when geometric.
class Points {
public:
    Points(double low, double high, bool geometric) : low_(low), high_(high), log_(geometric) {}

    double next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const double unit = static_cast<double>(state_ >> 11) * 0x1p-53;
        if (log_) {
            return std::exp(std::log(low_) + unit * (std::log(high_) - std::log(low_)));
        }
        return low_ + unit * (high_ - low_);
    }

private:
    double low_;
    double high_;
    bool log_;
    std::uint64_t state_ = 0x9E3779B97F4A7C15ULL;
};

constexpr int kSamples = 20000;

template <typename Ours, typename Reference>
void checkAccuracy(const char* name, Ours ours, Reference reference, Points points,
                   double maxUlps) {
    double worst = 0.0;
    double at = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const double x = points.next();
        const double error = ulps(ours(x), reference(x));
        if (error > worst) {
            worst = error;
            at = x;
        }
    }
    std::printf("%-9s worst %.0f ulp at %.17g\n", name, worst, at);
    TEST_CHECK(worst <= maxUlps);
}

// sin and cos of 2 pi t, the reduction to the nearest quarter turn done exactly.
double referenceTurns(double t, bool cosine) {
    const double quarters = std::nearbyint(4.0 * t); // ties to even, as dmath
    const double angle = (t - 0.25 * quarters) * 6.283185307179586;
    const auto quarter = static_cast<int>(quarters - 4.0 * std::floor(0.25 * quarters));
    const std::array<double, 4> values = {std::sin(angle), std::cos(angle), -std::sin(angle),
                                          -std::cos(angle)};
    return values[static_cast<std::size_t>((quarter + (cosine ? 1 : 0)) % 4)];
}

void testAccuracy() {
    // The references have errors of their own (about 1 ulp): limits leave room for both.
    checkAccuracy(
        "sinTurns", dmath::sinTurns, [](double t) { return referenceTurns(t, false); },
        Points(-3.0, 3.0, false), 3.0);
    checkAccuracy(
        "cosTurns", dmath::cosTurns, [](double t) { return referenceTurns(t, true); },
        Points(-3.0, 3.0, false), 3.0);
    checkAccuracy(
        "sin", dmath::sin, [](double x) { return std::sin(x); }, Points(-100.0, 100.0, false), 3.0);
    checkAccuracy(
        "cos", dmath::cos, [](double x) { return std::cos(x); }, Points(-100.0, 100.0, false), 3.0);
    checkAccuracy(
        "exp", dmath::exp, [](double x) { return std::exp(x); }, Points(-700.0, 700.0, false), 3.0);
    checkAccuracy(
        "exp2", dmath::exp2, [](double x) { return std::exp2(x); }, Points(-1000.0, 1000.0, false),
        3.0);
    checkAccuracy(
        "expm1", dmath::expm1, [](double x) { return std::expm1(x); }, Points(-5.0, 5.0, false),
        3.0);
    checkAccuracy(
        "expm1 0", dmath::expm1, [](double x) { return std::expm1(x); }, Points(1e-20, 0.5, true),
        3.0);
    checkAccuracy(
        "log", dmath::log, [](double x) { return std::log(x); }, Points(1e-300, 1e300, true), 3.0);
    checkAccuracy(
        "log 1", dmath::log, [](double x) { return std::log(x); }, Points(0.5, 2.0, false), 3.0);
    checkAccuracy(
        "log2", dmath::log2, [](double x) { return std::log2(x); }, Points(1e-300, 1e300, true),
        3.0);
    checkAccuracy(
        "log2 1", dmath::log2, [](double x) { return std::log2(x); }, Points(0.5, 2.0, false), 4.0);
    checkAccuracy(
        "tanh", dmath::tanh, [](double x) { return std::tanh(x); }, Points(-25.0, 25.0, false),
        3.0);
    checkAccuracy(
        "tanh 0", dmath::tanh, [](double x) { return std::tanh(x); }, Points(1e-20, 2.0, true),
        3.0);
    checkAccuracy(
        "atan", dmath::atan, [](double x) { return std::atan(x); }, Points(1e-10, 1e10, true), 4.0);
    checkAccuracy(
        "pow ^4", [](double x) { return dmath::pow(x, 4.0); },
        [](double x) { return std::pow(x, 4.0); }, Points(0.0, 1.0, false), 3.0);
    // pow with a fractional exponent loses about |y log2(x)| * 2e-16 relative.
    checkAccuracy(
        "pow ^2.5", [](double x) { return dmath::pow(x, 2.5); },
        [](double x) { return std::pow(x, 2.5); }, Points(1e-3, 1e3, true), 40.0);
    checkAccuracy(
        "atan2", [](double t) { return dmath::atan2(std::sin(t), 0.7 * std::cos(t)); },
        [](double t) { return std::atan2(std::sin(t), 0.7 * std::cos(t)); },
        Points(-3.2, 3.2, false), 4.0);
    checkAccuracy(
        "hypot", [](double x) { return dmath::hypot(x, 0.37 * x + 1.0); },
        [](double x) { return std::hypot(x, 0.37 * x + 1.0); }, Points(-1e6, 1e6, false), 2.0);
}

void testExactValues() {
    int mismatches = 0;
    for (const Exact& e : kExact) {
        const double value = evaluate(e.fn, e.x, e.y);
        if (std::bit_cast<std::uint64_t>(value) != e.bits) {
            ++mismatches;
            std::printf("function %d at (%a, %a): 0x%016" PRIX64 ", expected 0x%016" PRIX64 "\n",
                        static_cast<int>(e.fn), e.x, e.y, std::bit_cast<std::uint64_t>(value),
                        e.bits);
        }
    }
    TEST_CHECK(mismatches == 0);
}

void testSpecialValues() {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::array<double, 4> quarterSines = {0.0, 1.0, 0.0, -1.0};
    for (int k = -8; k <= 8; ++k) {
        const auto quarter = static_cast<std::size_t>((k % 4 + 4) % 4);
        TEST_CHECK(dmath::sinTurns(0.25 * k) == quarterSines[quarter]);
        TEST_CHECK(dmath::cosTurns(0.25 * k) == quarterSines[(quarter + 1) % 4]);
        TEST_CHECK(!std::signbit(dmath::sinTurns(0.5 * k))); // no negative zero
    }
    TEST_CHECK(dmath::sin(0.0) == 0.0 && dmath::cos(0.0) == 1.0);
    TEST_CHECK(std::isnan(dmath::sinTurns(inf)) && std::isnan(dmath::cos(nan)));
    TEST_CHECK(dmath::exp(0.0) == 1.0 && dmath::exp2(0.0) == 1.0 && dmath::expm1(0.0) == 0.0);
    TEST_CHECK(dmath::exp2(10.0) == 1024.0 && dmath::exp2(-3.0) == 0.125);
    TEST_CHECK(dmath::exp(1000.0) == inf && dmath::exp(-1000.0) == 0.0);
    TEST_CHECK(dmath::exp2(2000.0) == inf && dmath::exp2(-2000.0) == 0.0);
    TEST_CHECK(dmath::log(1.0) == 0.0 && dmath::log2(1.0) == 0.0 && dmath::log2(1024.0) == 10.0);
    TEST_CHECK(dmath::log(0.0) == -inf && dmath::log2(inf) == inf);
    TEST_CHECK(std::isnan(dmath::log(-1.0)) && std::isnan(dmath::log2(nan)));
    TEST_CHECK(dmath::pow(3.0, 0.0) == 1.0 && dmath::pow(0.0, 2.5) == 0.0);
    TEST_CHECK(dmath::pow(0.0, -1.5) == inf && std::isnan(dmath::pow(-2.0, 0.5)));
    TEST_CHECK(dmath::pow(-2.0, 3.0) == -8.0 && dmath::pow(2.0, -2.0) == 0.25);
    TEST_CHECK(dmath::tanh(0.0) == 0.0 && dmath::tanh(30.0) == 1.0 && dmath::tanh(-30.0) == -1.0);
    TEST_CHECK(dmath::tanh(1e-30) == 1e-30);
    TEST_CHECK(dmath::atan(0.0) == 0.0 && dmath::atan(inf) == 0x1.921fb54442d18p+0);
    TEST_CHECK(dmath::atan2(0.0, 1.0) == 0.0 && dmath::atan2(1.0, 0.0) == 0x1.921fb54442d18p+0);
    TEST_CHECK(dmath::atan2(0.0, -1.0) == 0x1.921fb54442d18p+1);
    TEST_CHECK(dmath::atan2(-0.0, -1.0) == -0x1.921fb54442d18p+1);
    TEST_CHECK(std::signbit(dmath::atan2(-0.0, 1.0)) && dmath::atan2(-1.0, 0.0) < 0.0);
    TEST_CHECK(dmath::atan2(inf, -inf) == 0x1.2d97c7f3321d2p+1);
    TEST_CHECK(dmath::hypot(3.0, 4.0) == 5.0 && dmath::hypot(2.0, 3.0, 6.0) == 7.0);
}

// Identities that hold bit for bit, whatever the rounding of the polynomials.
void testExactIdentities() {
    for (int i = -4096; i <= 4096; ++i) {
        const double t = i / 1024.0;
        for (const double turns : {1.0, -3.0, 1000.0}) {
            TEST_CHECK(dmath::sinTurns(t + turns) == dmath::sinTurns(t));
            TEST_CHECK(dmath::cosTurns(t + turns) == dmath::cosTurns(t));
        }
        TEST_CHECK(dmath::sinTurns(-t) == -dmath::sinTurns(t) + 0.0);
        TEST_CHECK(dmath::cosTurns(-t) == dmath::cosTurns(t));
        const double x = t * 1.7 + 0.3;
        TEST_CHECK(dmath::pow(x, 2.0) == x * x);
        TEST_CHECK(dmath::pow(x, 3.0) == x * (x * x));
        TEST_CHECK(dmath::pow(x, 4.0) == (x * x) * (x * x));
        TEST_CHECK(dmath::pow(x, -1.0) == 1.0 / x);
        TEST_CHECK(dmath::tanh(-x) == -dmath::tanh(x));
        TEST_CHECK(dmath::atan(-x) == -dmath::atan(x));
        TEST_CHECK(dmath::sin(-x) == -dmath::sin(x) && dmath::cos(-x) == dmath::cos(x));
    }
}

// Every function gives the same bits when called again, in another order.
void testDeterminism() {
    std::array<double, 64> first{};
    for (std::size_t i = 0; i < first.size(); ++i) {
        const double x = 0.37 * static_cast<double>(i) - 9.1;
        first[i] = dmath::sinTurns(x) + dmath::exp(x) + dmath::tanh(x) + dmath::atan2(x, 1.3) +
                   dmath::log(std::abs(x) + 0.1) + dmath::pow(std::abs(x), 1.7);
    }
    for (std::size_t i = first.size(); i-- > 0;) {
        const double x = 0.37 * static_cast<double>(i) - 9.1;
        const double again = dmath::sinTurns(x) + dmath::exp(x) + dmath::tanh(x) +
                             dmath::atan2(x, 1.3) + dmath::log(std::abs(x) + 0.1) +
                             dmath::pow(std::abs(x), 1.7);
        TEST_CHECK(std::bit_cast<std::uint64_t>(again) == std::bit_cast<std::uint64_t>(first[i]));
    }
}

} // namespace

int main() {
    testAccuracy();
    testExactValues();
    testSpecialValues();
    testExactIdentities();
    testDeterminism();
    return test::result();
}
