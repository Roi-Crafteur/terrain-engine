// Benchmark of the noise and the relief, not a test: the cost of heightAt and sampleAt, of the
// tiles of LOD 0 to 3 with and without caves and of 1024 x 1024 height grids, on the default world
// and on the planet (bench_world.cpp), and of each part of a height sample (lattices, seeds,
// wrapping, each layer of the relief, plates, cave noises). Each
// line gives the median cost per item over the runs, the fastest run, the items per run and an
// FNV checksum of the results: an optimization of the noise must leave every checksum unchanged.
// Build in Release and run on a quiet machine:
//   terrain_bench_noise [runs] [--quick]
// runs: runs per case (5 by default, at most 3 for the grids); --quick: 4 times fewer points and
// tiles, 256 x 256 grids.

#include "bench_support.hpp"
#include "bench_world.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using bench::Checksum;
using bench::Point;
using terrain::Heightfield;

using bench::Config;
using bench::kCenterChunkX;
using bench::kCenterChunkZ;
using bench::kSeed;
using bench::record;

// Scale of the inputs of the math profiles: abs(x) / kMixedRange.
constexpr double kMixedRange = 1'000'000.0;
// Candidates drawn per point of a kind: a kind that the relief lacks stays short instead of
// looping.
constexpr std::size_t kAttemptsPerPoint = 40;

// Points by kind, drawn over the whole world so that two reliefs compare alike. The kinds
// overlap: a mountain on the coast is in both.
struct PointSets {
    std::vector<Point> mixed;    // anywhere
    std::vector<Point> land;     // above the sea, without mountain mask
    std::vector<Point> coast;    // within 100 m of sea level
    std::vector<Point> mountain; // mountain mask above 0
    std::vector<Point> ocean;    // deeper than 1,000 m
};

PointSets pointSets(const Heightfield& field, std::size_t count) {
    PointSets sets;
    bench::Points points;
    const terrain::WorldSize& world = field.world();
    const auto add = [count](std::vector<Point>& set, Point p, bool member) {
        if (member && set.size() < count) {
            set.push_back(p);
        }
    };
    for (std::size_t attempt = 0; attempt < kAttemptsPerPoint * count; ++attempt) {
        const Point p{points.next(0.5 * world.x), points.next(0.5 * world.z)};
        const terrain::HeightSample s = field.sampleAt(p.x, p.z);
        add(sets.mixed, p, true);
        add(sets.land, p, s.height >= 0.0f && s.mountainMask == 0.0f);
        add(sets.coast, p, std::abs(s.height) < 100.0f);
        add(sets.mountain, p, s.mountainMask > 0.0f);
        add(sets.ocean, p, s.height < -1000.0f);
        if (sets.land.size() == count && sets.coast.size() == count &&
            sets.mountain.size() == count && sets.ocean.size() == count) {
            break;
        }
    }
    std::printf("points over the whole world: %zu mixed, %zu land, %zu coast, %zu mountain, %zu "
                "ocean\n",
                sets.mixed.size(), sets.land.size(), sets.coast.size(), sets.mountain.size(),
                sets.ocean.size());
    return sets;
}

std::uint64_t heights(const Heightfield& field, const std::vector<Point>& points) {
    Checksum sum;
    for (const Point& p : points) {
        sum.add(field.heightAt(p.x, p.z));
    }
    return sum.value();
}

void pointCases(const Config& c, const Heightfield& field, const Heightfield& planet,
                const PointSets& sets) {
    bench::section("height samples, ns per point");
    const std::size_t n = sets.mixed.size();
    const auto run = [&c](const Heightfield& f, const std::vector<Point>& points) {
        return bench::measure(c.runs, [&f, &points] { return heights(f, points); });
    };
    const auto kind = [&](const char* name, const std::vector<Point>& points) {
        record(name, run(field, points), points.size(), "ns");
    };
    kind("heightAt, mixed", sets.mixed);
    kind("heightAt, land", sets.land);
    kind("heightAt, coast", sets.coast);
    kind("heightAt, mountains", sets.mountain);
    kind("heightAt, ocean", sets.ocean);
    const auto samples = bench::measure(c.runs, [&field, &sets] {
        Checksum sum;
        for (const Point& p : sets.mixed) {
            const terrain::HeightSample s = field.sampleAt(p.x, p.z);
            for (const float v : {s.continentalness, s.mountainMask, s.base, s.mountain, s.detail,
                                  s.height}) {
                sum.add(v);
            }
        }
        return sum.value();
    });
    record("sampleAt, mixed", samples, n, "ns");
    record("heightAt planet (macro, plates), mixed", run(planet, sets.mixed), n, "ns");
}

// heightAt of the planetary geography (phase 11R), on points drawn by kind on its own world.
void geographyCases(const Config& c, const Heightfield& geography, const PointSets& sets) {
    bench::section("height samples of the geography, ns per point");
    const auto kind = [&](const char* name, const std::vector<Point>& points) {
        record(name, bench::measure(c.runs, [&] { return heights(geography, points); }),
               points.size(), "ns");
    };
    kind("geography heightAt, mixed", sets.mixed);
    kind("geography heightAt, land", sets.land);
    kind("geography heightAt, coast", sets.coast);
    kind("geography heightAt, mountains", sets.mountain);
    kind("geography heightAt, ocean", sets.ocean);
}

// Times f(point) over the points; f returns a double, added to the checksum.
template <class F>
void pointProfile(const Config& c, const char* name, const std::vector<Point>& points, F&& f) {
    const auto timing = bench::measure(c.runs, [&points, &f] {
        Checksum sum;
        for (const Point& p : points) {
            sum.add(static_cast<double>(f(p)));
        }
        return sum.value();
    });
    record(name, timing, points.size(), "ns");
}

void periodicityProfile(const Config& c, const std::vector<Point>& points) {
    bench::section("periodic lattices, ns per call");
    const terrain::WorldSize world;
    namespace noise = terrain::noise;
    pointProfile(c, "(loop and checksum alone)", points, [](Point p) { return p.x; });
    pointProfile(c, "wrapCoordinate", points, [&world](Point p) {
        return terrain::wrapCoordinate(p.x, world.x);
    });
    pointProfile(c, "deriveSeed", points, [](Point p) {
        const std::uint64_t stream = std::bit_cast<std::uint64_t>(p.x) & 7U;
        return static_cast<double>(noise::deriveSeed(kSeed, stream));
    });
    pointProfile(c, "lattice2D (one octave)", points, [&world](Point p) {
        const auto octave = static_cast<int>(std::bit_cast<std::uint64_t>(p.z) & 7U);
        return noise::lattice2D(world, 1.0 / (1000.0 + std::abs(p.x)), octave).scaleX;
    });
    const noise::Lattice2D straight = noise::lattice2D(world, 1.0 / 24000.0, 0);
    const noise::Lattice2D rotated = noise::lattice2D(world, 1.0 / 12000.0, 1);
    pointProfile(c, "gradient2D, built lattice, no rotation", points, [&straight](Point p) {
        return noise::gradient2D(p.x, p.z, kSeed, straight);
    });
    pointProfile(c, "gradient2D, built lattice, rotated", points, [&rotated](Point p) {
        return noise::gradient2D(p.x, p.z, kSeed, rotated);
    });
}

void layerProfile(const Config& c, const Heightfield& field, const std::vector<Point>& points) {
    bench::section("layers of the relief (mixed points), ns per call");
    namespace noise = terrain::noise;
    const terrain::HeightfieldParams& h = field.params();
    const terrain::WorldSize world;
    struct Layer {
        const char* name;
        noise::FbmParams params;
    };
    const Layer layers[] = {
        {"fbm2D continent, 4 octaves", {.octaves = 4, .frequency = 1.0 / h.continentWavelength}},
        {"fbm2D mountain mask, 3 octaves", {.octaves = 3, .frequency = 1.0 / h.maskWavelength}},
        {"fbm2D warp (one of 2), 3 octaves", {.octaves = 3, .frequency = 1.0 / h.warpWavelength}},
        {"fbm2D detail, 3 octaves", {.octaves = 3, .frequency = 1.0 / h.detailWavelength}},
    };
    for (const Layer& layer : layers) {
        pointProfile(c, layer.name, points, [&layer, &world](Point p) {
            return noise::fbm2D(p.x, p.z, kSeed, layer.params, world);
        });
    }
    const noise::FbmParams ridge{.octaves = h.ridgeOctaves, .frequency = 1.0 / h.ridgeWavelength};
    pointProfile(c, "ridged2D, 7 octaves, sharpness 4", points, [&](Point p) {
        return noise::ridged2D(p.x, p.z, kSeed, ridge, world, h.ridgeSharpness);
    });
}

// The octave lattices of FbmOctaves (OctaveLattices): the default rotated ones, rotated with
// 1 / c, unrotated.
void latticeProfile(const Config& c, const Heightfield& field, const std::vector<Point>& points) {
    bench::section("octave lattices (FbmOctaves, mixed points), ns per call");
    namespace noise = terrain::noise;
    const terrain::HeightfieldParams& h = field.params();
    struct Mode {
        const char* name;
        noise::OctaveLattices lattices;
    };
    const Mode modes[] = {{"rotated", noise::OctaveLattices::Rotated},
                          {"rotated 1/c", noise::OctaveLattices::RotatedFast},
                          {"unrotated", noise::OctaveLattices::Unrotated}};
    for (const Mode& mode : modes) {
        const noise::FbmOctaves fbm(kSeed,
                                    {.octaves = 4,
                                     .frequency = 1.0 / h.continentWavelength,
                                     .lattices = mode.lattices},
                                    field.world());
        const noise::FbmOctaves ridged(kSeed,
                                       {.octaves = h.ridgeOctaves,
                                        .frequency = 1.0 / h.ridgeWavelength,
                                        .lattices = mode.lattices},
                                       field.world());
        const std::string fbmName = std::string("fbm2D, 4 octaves, ") + mode.name;
        const std::string ridgedName = std::string("ridged2D, 7 octaves, ") + mode.name;
        pointProfile(c, fbmName.c_str(), points,
                     [&fbm](Point p) { return fbm.fbm2D(p.x, p.z); });
        pointProfile(c, ridgedName.c_str(), points, [&ridged, &h](Point p) {
            return ridged.ridged2D(p.x, p.z, h.ridgeSharpness);
        });
    }
}

void mathProfile(const Config& c, const Heightfield& planet, const std::vector<Point>& points) {
    bench::section("deterministic math and plates, ns per call");
    namespace dmath = terrain::dmath;
    const auto unit = [](Point p) { return std::abs(p.x) / kMixedRange; };
    pointProfile(c, "dmath::pow(r, 4.0)", points, [&unit](Point p) {
        return dmath::pow(unit(p), 4.0);
    });
    pointProfile(c, "dmath::pow(r, 2.5)", points, [&unit](Point p) {
        return dmath::pow(unit(p), 2.5);
    });
    pointProfile(c, "dmath::tanh", points, [](Point p) { return dmath::tanh(p.x / 300000.0); });
    pointProfile(c, "std::floor", points, [](Point p) { return std::floor(p.x / 1000.0); });
    const terrain::TectonicPlates& plates = *planet.plates();
    pointProfile(c, "TectonicPlates::sampleAt", points, [&plates](Point p) {
        return static_cast<double>(plates.sampleAt(p.x, p.z).continentalness);
    });
}

void caveNoiseProfile(const Config& c) {
    bench::section("cave noise nodes, ns per node value");
    const terrain::CaveField caves(kSeed);
    const terrain::CaveNoiseLattice& lattice = caves.lattice();
    const std::int64_t side = c.divisor == 1 ? 32 : 16;
    const auto nodes = [&lattice, side](terrain::CaveNoise noise) {
        Checksum sum;
        const std::int64_t i0 = kCenterChunkX * 4;
        const std::int64_t k0 = kCenterChunkZ * 4;
        for (std::int64_t k = k0; k < k0 + side; ++k) {
            for (std::int64_t j = -side; j < 0; ++j) {
                for (std::int64_t i = i0; i < i0 + side; ++i) {
                    sum.add(lattice.nodeValue(noise, i, j, k));
                }
            }
        }
        return sum.value();
    };
    const auto count = static_cast<std::size_t>(side * side * side);
    record("node value, spaghetti A (1 octave)",
           bench::measure(c.runs, [&nodes] { return nodes(terrain::kSpaghettiA); }), count, "ns");
    record("node value, cheese (2 octaves)",
           bench::measure(c.runs, [&nodes] { return nodes(terrain::kCheese); }), count, "ns");
}

void printBuild() {
#if defined(__clang__)
    std::printf("compiler: Clang %d.%d.%d\n", __clang_major__, __clang_minor__,
                __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    std::printf("compiler: MSVC %d\n", _MSC_VER);
#endif
#if !defined(NDEBUG)
    std::printf("warning: assertions are on, build in Release to measure\n");
#endif
}

} // namespace

int main(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) {
            c.divisor = 4;
        } else {
            c.runs = std::max(1, std::atoi(argv[i]));
        }
    }
    printBuild();
    std::printf("runs per case: %d (median, best)\n", c.runs);
    const Heightfield field(kSeed);
    const Heightfield planet(kSeed, bench::planetParams());
    const PointSets sets = pointSets(field, 200'000 / c.divisor);
    pointCases(c, field, planet, sets);
    terrain::HeightfieldParams geographyParams;
    geographyParams.geography.enabled = true;
    const Heightfield geography(kSeed, geographyParams);
    geographyCases(c, geography, pointSets(geography, 200'000 / c.divisor));
    periodicityProfile(c, sets.mixed);
    layerProfile(c, field, sets.mixed);
    latticeProfile(c, field, sets.mixed);
    mathProfile(c, planet, sets.mixed);
    caveNoiseProfile(c);
    bench::worldCases(c, field, planet);
    std::printf("\nchecksum of every result: %016llx\n",
                static_cast<unsigned long long>(bench::total().value()));
    return 0;
}
