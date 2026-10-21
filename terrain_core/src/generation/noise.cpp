#include "terrain/generation/noise.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace terrain::noise {

namespace {

constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kCellZPrime = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t kCellYPrime = 0xD6E8FEB86659FD93ULL;

// splitmix64 finalizer: every input bit affects every output bit.
constexpr std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Exact rotations (a / c, b / c) of the octaves: see kRotationCount.
struct Rotation {
    std::int64_t a;
    std::int64_t b;
    std::int64_t c;
};
constexpr std::array<Rotation, kRotationCount> kRotations = {{
    {1, 0, 1},
    {4, 3, 5},
    {5, 12, 13},
    {24, 7, 25},
    {3, 4, 5},
    {15, 8, 17},
    {7, 24, 25},
    {12, 5, 13},
}};

// Offsets along y of the octaves of the 3D noises, in cells.
constexpr std::array<double, kRotationCount> kOffsetsY = {0.0,   0.5,   0.25,  0.75,
                                                          0.125, 0.625, 0.375, 0.875};

// 16 unit gradients evenly spaced on the circle.
constexpr double kCos22 = 0.92387953251128674;
constexpr double kSin22 = 0.38268343236508978;
constexpr double kDiag = 0.70710678118654752;
constexpr std::array<std::array<double, 2>, 16> kGradients = {{
    {1.0, 0.0},       {kCos22, kSin22},   {kDiag, kDiag},   {kSin22, kCos22},
    {0.0, 1.0},       {-kSin22, kCos22},  {-kDiag, kDiag},  {-kCos22, kSin22},
    {-1.0, 0.0},      {-kCos22, -kSin22}, {-kDiag, -kDiag}, {-kSin22, -kCos22},
    {0.0, -1.0},      {kSin22, -kCos22},  {kDiag, -kDiag},  {kCos22, -kSin22},
}};

// The 12 edge directions of the cube (+-1, +-1, 0), (+-1, 0, +-1), (0, +-1, +-1), then 4 of
// them again to fill 16 entries, all scaled to unit length. Components along (u, y, v).
constexpr std::array<std::array<double, 3>, 16> kGradients3 = {{
    {kDiag, kDiag, 0.0},   {-kDiag, kDiag, 0.0},  {kDiag, -kDiag, 0.0},  {-kDiag, -kDiag, 0.0},
    {kDiag, 0.0, kDiag},   {-kDiag, 0.0, kDiag},  {kDiag, 0.0, -kDiag},  {-kDiag, 0.0, -kDiag},
    {0.0, kDiag, kDiag},   {0.0, -kDiag, kDiag},  {0.0, kDiag, -kDiag},  {0.0, -kDiag, -kDiag},
    {kDiag, kDiag, 0.0},   {-kDiag, kDiag, 0.0},  {0.0, -kDiag, kDiag},  {0.0, -kDiag, -kDiag},
}};

constexpr double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Cells along a period of the world: a whole number of super-cells of c cells.
std::int64_t cellsOn(double period, double frequency, std::int64_t c) {
    const double superCells =
        std::max(1.0, std::round(period * frequency / static_cast<double>(c)));
    return static_cast<std::int64_t>(superCells) * c;
}

// Invariants of a point of a rotated lattice: equal for all its images (Lattice2D).
struct Invariants {
    std::int64_t s;
    std::int64_t t;
};

// v mod m in [0, m). The invariants of the points of the canonical range mostly lie within a
// period of [0, m): corrected without a division.
std::int64_t modulo(std::int64_t v, std::int64_t m) {
    if (v >= 0) {
        if (v < m) {
            return v;
        }
        if (v - m < m) {
            return v - m;
        }
    } else if (v >= -m) {
        return v + m;
    }
    const std::int64_t r = v % m;
    return r < 0 ? r + m : r;
}

// (v + step) mod m for v in [0, m) and |step| < m.
std::int64_t step(std::int64_t v, std::int64_t delta, std::int64_t m) {
    const std::int64_t r = v + delta;
    if (r >= m) {
        return r - m;
    }
    return r < 0 ? r + m : r;
}

// Position in the rotated lattice of a point of the canonical range.
struct LatticePoint {
    double u;
    double v;
};

LatticePoint toLattice(double x, double z, const Lattice2D& l) {
    const double px = x * l.scaleX;
    const double pz = z * l.scaleZ;
    if (l.c == 1) {
        // Without rotation (1 px - 0 pz) / 1 is px: the same bits without the divisions, the
        // sign of a zero aside, which the offset (+0 at least) clears.
        return {px + l.offsetU, pz + l.offsetV};
    }
    const auto a = static_cast<double>(l.a);
    const auto b = static_cast<double>(l.b);
    const auto c = static_cast<double>(l.c);
    return {(a * px - b * pz) / c + l.offsetU, (b * px + a * pz) / c + l.offsetV};
}

// The cell of the rotated lattice holding a point: invariants of its 4 corners, (u0, v0) then
// (u0 + 1, v0), (u0, v0 + 1) and (u0 + 1, v0 + 1), and the position inside it.
struct LatticeCell {
    std::array<Invariants, 4> corners;
    double fu;
    double fv;
};

LatticeCell cellOf(LatticePoint p, const Lattice2D& l) {
    const double u0 = std::floor(p.u);
    const double v0 = std::floor(p.v);
    const auto iu = static_cast<std::int64_t>(u0);
    const auto iv = static_cast<std::int64_t>(v0);
    LatticeCell cell{};
    cell.fu = p.u - u0;
    cell.fv = p.v - v0;
    const Invariants base{modulo(l.a * iu + l.b * iv, l.periodS),
                          modulo(l.a * iv - l.b * iu, l.periodT)};
    const Invariants alongU{step(base.s, l.a, l.periodS), step(base.t, -l.b, l.periodT)};
    cell.corners = {base, alongU,
                    Invariants{step(base.s, l.b, l.periodS), step(base.t, l.a, l.periodT)},
                    Invariants{step(alongU.s, l.b, l.periodS), step(alongU.t, l.a, l.periodT)}};
    return cell;
}

std::uint64_t hashOf(Invariants corner, std::uint64_t seed) {
    return mix64(mix64(seed ^ (static_cast<std::uint64_t>(corner.s) * kGolden)) ^
                 (static_cast<std::uint64_t>(corner.t) * kCellZPrime));
}

// Dot product of the gradient of a corner with the offset (du, dv) to it.
double cornerDot(Invariants corner, double du, double dv, std::uint64_t seed) {
    const auto& g = kGradients[hashOf(corner, seed) & 15U];
    return g[0] * du + g[1] * dv;
}

// gradient2D of a point of the canonical range.
float latticeNoise(double x, double z, std::uint64_t seed, const Lattice2D& lattice) {
    const LatticeCell c = cellOf(toLattice(x, z, lattice), lattice);
    const double n00 = cornerDot(c.corners[0], c.fu, c.fv, seed);
    const double n10 = cornerDot(c.corners[1], c.fu - 1.0, c.fv, seed);
    const double n01 = cornerDot(c.corners[2], c.fu, c.fv - 1.0, seed);
    const double n11 = cornerDot(c.corners[3], c.fu - 1.0, c.fv - 1.0, seed);
    const double u = fade(c.fu);
    const double v = fade(c.fv);
    const double nv0 = n00 + u * (n10 - n00);
    const double nv1 = n01 + u * (n11 - n01);
    // 2D Perlin noise with unit gradients stays within +-sqrt(2) / 2.
    const double value = (nv0 + v * (nv1 - nv0)) * std::numbers::sqrt2;
    return static_cast<float>(std::clamp(value, -1.0, 1.0));
}

// Dot product of the gradient of a 3D corner with the offset (du, dy, dv) to it.
double cornerDot3(Invariants corner, std::int64_t iy, double du, double dy, double dv,
                  std::uint64_t seed) {
    const std::uint64_t h =
        mix64(hashOf(corner, seed) ^ (static_cast<std::uint64_t>(iy) * kCellYPrime));
    const auto& g = kGradients3[h & 15U];
    return g[0] * du + g[1] * dy + g[2] * dv;
}

// gradient3D of a point of the canonical range.
float latticeNoise3(double x, double y, double z, std::uint64_t seed, const Lattice2D& lattice,
                    double frequencyY, double offsetY) {
    const LatticeCell c = cellOf(toLattice(x, z, lattice), lattice);
    const double w = y * frequencyY + offsetY;
    const double w0 = std::floor(w);
    const auto iy = static_cast<std::int64_t>(w0);
    const double fy = w - w0;
    std::array<double, 8> n{}; // corner (u, y, v) at index u + 2 y + 4 v
    for (int corner = 0; corner < 8; ++corner) {
        const int du = corner & 1;
        const int dy = (corner >> 1) & 1;
        const int dv = (corner >> 2) & 1;
        n[static_cast<std::size_t>(corner)] =
            cornerDot3(c.corners[static_cast<std::size_t>(du + 2 * dv)], iy + dy, c.fu - du,
                       fy - dy, c.fv - dv, seed);
    }
    const double su = fade(c.fu);
    const double sy = fade(fy);
    const double sv = fade(c.fv);
    const double y0v0 = n[0] + su * (n[1] - n[0]);
    const double y1v0 = n[2] + su * (n[3] - n[2]);
    const double y0v1 = n[4] + su * (n[5] - n[4]);
    const double y1v1 = n[6] + su * (n[7] - n[6]);
    const double v0 = y0v0 + sy * (y1v0 - y0v0);
    const double v1 = y0v1 + sy * (y1v1 - y0v1);
    // 3D Perlin noise with unit gradients stays within +-sqrt(3) / 2.
    const double value = (v0 + sv * (v1 - v0)) * (2.0 * std::numbers::inv_sqrt3);
    return static_cast<float>(std::clamp(value, -1.0, 1.0));
}

} // namespace

std::uint64_t deriveSeed(std::uint64_t seed, std::uint64_t stream) {
    return mix64(seed + kGolden * (stream + 1));
}

Lattice2D lattice2D(const WorldSize& world, double frequency, int rotation, double offsetU,
                    double offsetV) {
    const Rotation& r = kRotations[static_cast<std::size_t>(rotation % kRotationCount)];
    const std::int64_t cellsX = cellsOn(world.x, frequency, r.c);
    const std::int64_t cellsZ = cellsOn(world.z, frequency, r.c);
    return {static_cast<double>(cellsX) / world.x,
            static_cast<double>(cellsZ) / world.z,
            r.a,
            r.b,
            r.c,
            cellsX * r.c,
            cellsZ * r.c,
            offsetU,
            offsetV,
            world};
}

float gradient2D(double x, double z, std::uint64_t seed, const Lattice2D& lattice) {
    return latticeNoise(wrapCoordinate(x, lattice.world.x), wrapCoordinate(z, lattice.world.z),
                        seed, lattice);
}

float fbm2D(double x, double z, std::uint64_t seed, const FbmParams& params,
            const WorldSize& world) {
    const double cx = wrapCoordinate(x, world.x);
    const double cz = wrapCoordinate(z, world.z);
    double frequency = params.frequency;
    double amplitude = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        sum += amplitude * latticeNoise(cx, cz, octaveSeed, lattice2D(world, frequency, octave));
        total += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, -1.0, 1.0)) : 0.0f;
}

float ridged2D(double x, double z, std::uint64_t seed, const FbmParams& params,
               const WorldSize& world, double sharpness) {
    const double cx = wrapCoordinate(x, world.x);
    const double cz = wrapCoordinate(z, world.z);
    double frequency = params.frequency;
    double amplitude = 1.0;
    double weight = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        const float n = latticeNoise(cx, cz, octaveSeed, lattice2D(world, frequency, octave));
        const double ridge = 1.0 - std::abs(static_cast<double>(n));
        const double signal = dmath::pow(ridge, sharpness) * weight;
        weight = std::clamp(2.0 * signal, 0.0, 1.0);
        sum += amplitude * signal;
        total += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, 0.0, 1.0)) : 0.0f;
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float gradient3D(double x, double y, double z, std::uint64_t seed, const Lattice2D& lattice,
                 double frequencyY, double offsetY) {
    return latticeNoise3(wrapCoordinate(x, lattice.world.x), y, wrapCoordinate(z, lattice.world.z),
                         seed, lattice, frequencyY, offsetY);
}

float fbm3D(double x, double y, double z, std::uint64_t seed, const FbmParams& params,
            const WorldSize& world) {
    const double cx = wrapCoordinate(x, world.x);
    const double cz = wrapCoordinate(z, world.z);
    double frequency = params.frequency;
    double amplitude = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        const double offsetY = kOffsetsY[static_cast<std::size_t>(octave % kRotationCount)];
        sum += amplitude * latticeNoise3(cx, y, cz, octaveSeed, lattice2D(world, frequency, octave),
                                         frequency, offsetY);
        total += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, -1.0, 1.0)) : 0.0f;
}

} // namespace terrain::noise
