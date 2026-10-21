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
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include "bit_exact_support.hpp"
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
using bit_exact::edgePoints;
using bit_exact::kWorlds;
using bit_exact::Point;
using bit_exact::randomPoints;
using bit_exact::same;
using bit_exact::Tally;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 20261019;

bool same(const noise::Lattice2D& a, const noise::Lattice2D& b) {
    return same(a.scaleX, b.scaleX) && same(a.scaleZ, b.scaleZ) && a.a == b.a && a.b == b.b &&
           a.c == b.c && a.periodS == b.periodS && a.periodT == b.periodT &&
           same(a.offsetU, b.offsetU) && same(a.offsetV, b.offsetV) && a.world == b.world;
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

// The free functions and FbmOctaves against the frozen free functions.
void checkNoisePoint(Tally& tally, const noise::FbmParams& fbm, const WorldSize& world,
                     const noise::FbmOctaves& octaves, Point p) {
    namespace old = frozen::noise;
    const double y = 0.37 * p.x - 0.11 * p.z;
    const float fbm2D = old::fbm2D(p.x, p.z, kSeed, fbm, world);
    tally.check(same(noise::fbm2D(p.x, p.z, kSeed, fbm, world), fbm2D), p, "fbm2D");
    tally.check(same(octaves.fbm2D(p.x, p.z), fbm2D), p, "FbmOctaves::fbm2D");
    for (const double sharpness : {1.0, 2.0, 2.5, 4.0}) {
        const float ridged = old::ridged2D(p.x, p.z, kSeed, fbm, world, sharpness);
        tally.check(same(noise::ridged2D(p.x, p.z, kSeed, fbm, world, sharpness), ridged), p,
                    "ridged2D");
        tally.check(same(octaves.ridged2D(p.x, p.z, sharpness), ridged), p,
                    "FbmOctaves::ridged2D");
    }
    const float fbm3D = old::fbm3D(p.x, y, p.z, kSeed, fbm, world);
    tally.check(same(noise::fbm3D(p.x, y, p.z, kSeed, fbm, world), fbm3D), p, "fbm3D");
    tally.check(same(octaves.fbm3D(p.x, y, p.z), fbm3D), p, "FbmOctaves::fbm3D");
    const noise::Lattice2D lattice = noise::lattice2D(world, fbm.frequency, 1, 0.25, 0.5);
    tally.check(same(noise::gradient2D(p.x, p.z, kSeed, lattice),
                     old::gradient2D(p.x, p.z, kSeed, lattice)),
                p, "gradient2D");
    tally.check(same(noise::gradient3D(p.x, y, p.z, kSeed, lattice, fbm.frequency, 0.5),
                     old::gradient3D(p.x, y, p.z, kSeed, lattice, fbm.frequency, 0.5)),
                p, "gradient3D");
}

void checkNoises() {
    Tally tally("noises and FbmOctaves");
    const std::vector<Point> random = randomPoints(12'000, 2e7);
    const std::vector<Point> near = randomPoints(4'000, 3000.0);
    for (const WorldSize& world : kWorlds) {
        const std::vector<Point> edges = edgePoints(world);
        for (const noise::FbmParams& fbm : kFbms) {
            const noise::FbmOctaves octaves(kSeed, fbm, world);
            for (const auto* set : {&random, &near, &edges}) {
                for (const Point& p : *set) {
                    checkNoisePoint(tally, fbm, world, octaves, p);
                }
            }
            for (const Point& p : latticeLinePoints(world, fbm.frequency)) {
                checkNoisePoint(tally, fbm, world, octaves, p);
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
    const std::vector<Point> rows = bit_exact::rowPoints(world, 256);
    points.insert(points.end(), rows.begin(), rows.end());
    // The batch functions over all the points at once: scattered, then in rows.
    std::vector<double> xs;
    std::vector<double> zs;
    for (const Point& p : points) {
        xs.push_back(p.x);
        zs.push_back(p.z);
    }
    std::vector<terrain::HeightSample> samples(points.size());
    std::vector<float> heights(points.size());
    live.samplesAt(xs, zs, samples);
    live.heightsAt(xs, zs, heights);
    for (std::size_t n = 0; n < points.size(); ++n) {
        const Point p = points[n];
        const terrain::HeightSample reference = old.sampleAt(p.x, p.z);
        tally.check(same(live.sampleAt(p.x, p.z), reference), p, "sampleAt");
        tally.check(same(samples[n], reference), p, "samplesAt");
        tally.check(same(heights[n], reference.height), p, "heightsAt");
        if (n % 8 == 0) {
            tally.check(same(live.heightAt(p.x, p.z), reference.height), p, "heightAt");
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
