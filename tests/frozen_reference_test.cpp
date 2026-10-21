// The live noise, Heightfield, plates and cave noise nodes against the frozen reference of
// commit 052078b (tests/frozen), bit for bit: about a million pseudo-random points and every edge
// case (edges and corner of the world, +-0, the neighbors of +-period / 2, far images, negatives,
// lattice lines). The golden checksums sample a few thousand points: a bit changed in a rare
// branch could escape them, not this test. Run at every step of the noise performance work.
#include "frozen/frozen_heightfield.hpp"
#include "frozen/frozen_noise.hpp"

#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include "test_assert.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

namespace noise = terrain::noise;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 20261019;
const std::array<WorldSize, 2> kWorlds = {WorldSize{}, WorldSize{65536.0, 3.0 * 65536.0}};

struct Point {
    double x;
    double z;
};

// Deterministic coordinates in [-range, range).
class Points {
public:
    double next(double range) {
        state_ = noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) * 0x1p-53;
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_ = 11;
};

std::vector<Point> randomPoints(std::size_t count, double range) {
    Points points;
    std::vector<Point> result;
    for (std::size_t i = 0; i < count; ++i) {
        const double x = points.next(range);
        result.push_back({x, points.next(range)});
    }
    return result;
}

// Coordinates along an axis of period p that exercise every branch of the wrapping: 0 and -0,
// +-p / 2 and their two neighbors on each side, their images 1, 2 and 1000 periods away, other
// negatives, and coordinates far beyond the world.
std::vector<double> edgeValues(double period) {
    const double half = 0.5 * period;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> base = {0.0, -0.0, 1.0 / 64.0, -1.0 / 64.0, 0.25 * period, -0.3 * period};
    for (const double bound : {half, -half}) {
        double below = bound;
        double above = bound;
        base.push_back(bound);
        for (int step = 0; step < 2; ++step) {
            below = std::nextafter(below, -inf);
            above = std::nextafter(above, inf);
            base.push_back(below);
            base.push_back(above);
        }
        base.push_back(bound - 1.0 / 64.0);
        base.push_back(bound + 1.0 / 64.0);
    }
    std::vector<double> values = base;
    for (const double k : {1.0, -1.0, 2.0, -2.0, 1000.0, -1000.0}) {
        for (const double v : base) {
            values.push_back(v + k * period);
        }
    }
    values.push_back(1e12 + 0.5);
    values.push_back(-1e12 - 0.25);
    return values;
}

std::vector<Point> edgePoints(const WorldSize& world) {
    std::vector<Point> points;
    const std::vector<double> xs = edgeValues(world.x);
    const std::vector<double> zs = edgeValues(world.z);
    for (const double z : zs) {
        for (const double x : xs) {
            points.push_back({x, z});
        }
    }
    return points;
}

// Mismatches of one family of checks, the first one printed.
class Tally {
public:
    explicit Tally(const char* name) : name_(name) {}
    ~Tally() {
        std::printf("%-34s %9ld checks, %ld different\n", name_, checked_, failed_);
        TEST_CHECK(failed_ == 0);
        TEST_CHECK(checked_ > 0);
    }
    Tally(const Tally&) = delete;
    Tally& operator=(const Tally&) = delete;

    void check(bool same, Point p, const char* what) {
        ++checked_;
        if (!same && failed_++ == 0) {
            std::printf("  first difference: %s at (%.17g, %.17g)\n", what, p.x, p.z);
        }
    }

private:
    const char* name_;
    long checked_ = 0;
    long failed_ = 0;
};

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool same(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same(const noise::Lattice2D& a, const noise::Lattice2D& b) {
    return same(a.scaleX, b.scaleX) && same(a.scaleZ, b.scaleZ) && a.a == b.a && a.b == b.b &&
           a.c == b.c && a.periodS == b.periodS && a.periodT == b.periodT &&
           same(a.offsetU, b.offsetU) && same(a.offsetV, b.offsetV) && a.world == b.world;
}

bool same(const terrain::HeightSample& a, const terrain::HeightSample& b) {
    return same(a.continentalness, b.continentalness) &&
           same(a.macroContinentalness, b.macroContinentalness) &&
           same(a.plateContinentalness, b.plateContinentalness) &&
           same(a.plateCollision, b.plateCollision) && same(a.mountainMask, b.mountainMask) &&
           same(a.base, b.base) && same(a.mountain, b.mountain) && same(a.detail, b.detail) &&
           same(a.height, b.height);
}

// Parameter sets of the fbm checks: the layers of the Heightfield and the caves, and unusual
// ones (no octave, more octaves than rotations, another lacunarity, a negative gain).
const std::array<noise::FbmParams, 7> kFbms = {{
    {.octaves = 4, .frequency = 1.0 / 60000.0},
    {.octaves = 7, .frequency = 1.0 / 24000.0},
    {.octaves = 3, .frequency = 1.0 / 200.0},
    {.octaves = 0, .frequency = 1.0 / 900.0},
    {.octaves = 9, .frequency = 1.0 / 5000.0, .lacunarity = 1.9, .gain = 0.55},
    {.octaves = 3, .frequency = 1.0 / 700.0, .lacunarity = 2.0, .gain = -0.4},
    {.octaves = 1, .frequency = 1.0 / 32.0},
}};

void checkLattices() {
    Tally tally("lattice2D");
    for (const WorldSize& world : kWorlds) {
        for (const double frequency : {1.0 / 60000.0, 1.0 / 900.0, 0.3, 1e-9, 1.0 / 7.0}) {
            for (int rotation = 0; rotation < 10; ++rotation) {
                for (const double offset : {0.0, 0.37, -2.5}) {
                    const auto live = noise::lattice2D(world, frequency, rotation, offset, -offset);
                    const auto old = frozen::noise::lattice2D(world, frequency, rotation, offset,
                                                              -offset);
                    tally.check(same(live, old), {frequency, offset}, "lattice2D");
                }
            }
        }
    }
}

// Points on, just below and just above lines of the unrotated lattice of the frequency.
std::vector<Point> latticeLinePoints(const WorldSize& world, double frequency) {
    const noise::Lattice2D lattice = noise::lattice2D(world, frequency, 0);
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<Point> points;
    for (const std::int64_t line : {-7LL, -1LL, 0LL, 1LL, 3LL, 250LL}) {
        const double x = static_cast<double>(line) / lattice.scaleX;
        const double z = static_cast<double>(-line) / lattice.scaleZ;
        for (const double dx : {std::nextafter(x, -inf), x, std::nextafter(x, inf)}) {
            for (const double dz : {std::nextafter(z, -inf), z, std::nextafter(z, inf)}) {
                points.push_back({dx, dz});
            }
        }
    }
    return points;
}

void checkNoisePoint(Tally& tally, const noise::FbmParams& fbm, const WorldSize& world, Point p) {
    namespace old = frozen::noise;
    const double y = 0.37 * p.x - 0.11 * p.z;
    tally.check(same(noise::fbm2D(p.x, p.z, kSeed, fbm, world),
                     old::fbm2D(p.x, p.z, kSeed, fbm, world)),
                p, "fbm2D");
    for (const double sharpness : {1.0, 2.0, 2.5, 4.0}) {
        tally.check(same(noise::ridged2D(p.x, p.z, kSeed, fbm, world, sharpness),
                         old::ridged2D(p.x, p.z, kSeed, fbm, world, sharpness)),
                    p, "ridged2D");
    }
    tally.check(same(noise::fbm3D(p.x, y, p.z, kSeed, fbm, world),
                     old::fbm3D(p.x, y, p.z, kSeed, fbm, world)),
                p, "fbm3D");
    const noise::Lattice2D lattice = noise::lattice2D(world, fbm.frequency, 1, 0.25, 0.5);
    tally.check(same(noise::gradient2D(p.x, p.z, kSeed, lattice),
                     old::gradient2D(p.x, p.z, kSeed, lattice)),
                p, "gradient2D");
    tally.check(same(noise::gradient3D(p.x, y, p.z, kSeed, lattice, fbm.frequency, 0.5),
                     old::gradient3D(p.x, y, p.z, kSeed, lattice, fbm.frequency, 0.5)),
                p, "gradient3D");
}

void checkNoises() {
    Tally tally("fbm2D, ridged2D, fbm3D, gradients");
    const std::vector<Point> random = randomPoints(12'000, 2e7);
    const std::vector<Point> near = randomPoints(4'000, 3000.0);
    for (const WorldSize& world : kWorlds) {
        const std::vector<Point> edges = edgePoints(world);
        for (const noise::FbmParams& fbm : kFbms) {
            for (const auto* set : {&random, &near, &edges}) {
                for (const Point& p : *set) {
                    checkNoisePoint(tally, fbm, world, p);
                }
            }
            for (const Point& p : latticeLinePoints(world, fbm.frequency)) {
                checkNoisePoint(tally, fbm, world, p);
            }
        }
    }
}

terrain::HeightfieldParams planetParams() {
    terrain::HeightfieldParams params;
    params.macroContinent = 0.25f;
    params.plates.amplitude = 0.5f;
    return params;
}

void checkField(const char* name, const terrain::HeightfieldParams& params,
                const WorldSize& world, std::size_t count) {
    Tally tally(name);
    const terrain::Heightfield live(kSeed, params, world);
    const frozen::Heightfield old(kSeed, params, world);
    std::vector<Point> points = randomPoints(count, 0.6 * world.x);
    const std::vector<Point> edges = edgePoints(world);
    points.insert(points.end(), edges.begin(), edges.end());
    // Every meter of a mountain chunk of the tests (steep, curved ground) and its neighbors.
    for (int k = 0; k < 40; ++k) {
        for (int i = 0; i < 40; ++i) {
            points.push_back({-1245184.0 + 7.0 * i + 0.5, -8500224.0 + 7.0 * k - 0.25});
        }
    }
    for (std::size_t n = 0; n < points.size(); ++n) {
        const Point p = points[n];
        const terrain::HeightSample s = live.sampleAt(p.x, p.z);
        tally.check(same(s, old.sampleAt(p.x, p.z)), p, "sampleAt");
        if (n % 8 == 0) {
            tally.check(same(live.heightAt(p.x, p.z), s.height), p, "heightAt");
        }
    }
}

void checkCaveNodes() {
    Tally tally("cave noise nodes");
    for (const WorldSize& world : kWorlds) {
        const terrain::CaveField caves(kSeed, {}, world);
        const terrain::CaveNoiseLattice& lattice = caves.lattice();
        const std::int64_t edgeI = lattice.nodesX().count / 2;
        const std::int64_t edgeK = lattice.nodesZ().count / 2;
        const std::array<std::int64_t, 4> startsI = {-3, edgeI - 3, -edgeI - 3, 77 * edgeI};
        const std::array<std::int64_t, 3> startsK = {-3, edgeK - 3, -edgeK - 3};
        for (const std::int64_t i0 : startsI) {
            for (const std::int64_t k0 : startsK) {
                for (std::int64_t k = k0; k < k0 + 6; ++k) {
                    for (std::int64_t j = -60; j < 4; j += 3) {
                        for (std::int64_t i = i0; i < i0 + 6; ++i) {
                            for (std::size_t n = 0; n < terrain::kCaveNoiseCount; ++n) {
                                const auto noise = static_cast<terrain::CaveNoise>(n);
                                tally.check(same(lattice.nodeValue(noise, i, j, k),
                                                 frozen::caveNodeValue(lattice, noise, i, j, k,
                                                                       world)),
                                            {static_cast<double>(i), static_cast<double>(k)},
                                            "nodeValue");
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace

int main() {
    checkLattices();
    checkNoises();
    checkField("Heightfield, default world", {}, WorldSize{}, 300'000);
    checkField("Heightfield, planet (macro, plates)", planetParams(), WorldSize{}, 80'000);
    checkField("Heightfield, small world", {}, kWorlds[1], 60'000);
    checkField("Heightfield, small world planet", planetParams(), kWorlds[1], 20'000);
    checkCaveNodes();
    return test::result();
}
