#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::HeightfieldParams;
using terrain::HeightSample;
using terrain::kSeaLevel;

constexpr std::array<std::uint64_t, 4> kSeeds = {terrain::kDefaultWorldSeed, 1, 2, 3};

// Every test sets the macro layer explicitly, so none depends on the default parameters.
constexpr float kAmplitude = 0.5f;
constexpr double kWavelength = 10'000'000.0; // 10,000 km
constexpr float kWidth = 0.1f;

HeightfieldParams macroParams(float amplitude) {
    HeightfieldParams params;
    params.macroContinent = amplitude;
    params.macroContinentWavelength = kWavelength;
    params.macroContinentWidth = kWidth;
    return params;
}

// Pseudo-random coordinates in [-range, range).
class Points {
public:
    explicit Points(std::uint64_t seed) : state_(seed) {}

    double next(double range) {
        state_ = terrain::noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) / 9007199254740992.0; // 2^53
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_;
};

// Land (above sea level) or ocean of a square grid of samples, row by row. Sample (i, j) is
// at the center of its cell: the grid is centered on (centerX, centerZ).
struct LandMap {
    int size = 0;
    std::vector<std::uint8_t> land;

    [[nodiscard]] bool isLand(int i, int j) const {
        return land[static_cast<std::size_t>(j * size + i)] != 0;
    }
};

LandMap sampleLandMap(const Heightfield& field, int size, double step, double centerX,
                      double centerZ) {
    LandMap map;
    map.size = size;
    map.land.reserve(static_cast<std::size_t>(size * size));
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const double x = centerX + (i + 0.5 - size / 2.0) * step;
            const double z = centerZ + (j + 0.5 - size / 2.0) * step;
            map.land.push_back(field.heightAt(x, z) >= kSeaLevel ? 1 : 0);
        }
    }
    return map;
}

// The same seed with and without the macro layer, sampled on the planet grid: 256 x 256
// samples 128 km apart around the origin, a 32,768 km square (3.3 macro wavelengths).
constexpr int kPlanetSize = 256;
constexpr double kPlanetStep = 128'000.0;

struct Planet {
    Heightfield on;
    Heightfield off;
    LandMap onMap;
    LandMap offMap;
};

Planet makePlanet(std::uint64_t seed) {
    Planet planet{Heightfield(seed, macroParams(kAmplitude)),
                  Heightfield(seed, macroParams(0.0f)), {}, {}};
    planet.onMap = sampleLandMap(planet.on, kPlanetSize, kPlanetStep, 0.0, 0.0);
    planet.offMap = sampleLandMap(planet.off, kPlanetSize, kPlanetStep, 0.0, 0.0);
    return planet;
}

// A window of kWindow x kWindow planet samples is 2048 km wide. "Very large" means that
// at least 95% of its samples are of the same kind. With the macro layer the best window is
// 256 of 256 samples for oceans and for land (every seed); without it, 153 to 165 of 256.
// The bound leaves a wide margin on both sides.
constexpr int kWindow = 16;
constexpr int kWindowCells = kWindow * kWindow;
constexpr int kPureCells = (95 * kWindowCells + 99) / 100;

// Number of land (or ocean) samples in every kWindow x kWindow window of the map, by a
// summed-area table. Windows are indexed by their first sample, row by row.
constexpr int kWindowsPerRow = kPlanetSize - kWindow + 1;

std::vector<int> windowCounts(const LandMap& map, bool wantLand) {
    const int n = map.size;
    const auto at = [n](int i, int j) { return static_cast<std::size_t>(j * (n + 1) + i); };
    std::vector<int> sum(static_cast<std::size_t>((n + 1) * (n + 1)), 0);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            sum[at(i + 1, j + 1)] = (map.isLand(i, j) == wantLand ? 1 : 0) + sum[at(i, j + 1)] +
                                    sum[at(i + 1, j)] - sum[at(i, j)];
        }
    }
    std::vector<int> counts;
    for (int j = 0; j + kWindow <= n; ++j) {
        for (int i = 0; i + kWindow <= n; ++i) {
            counts.push_back(sum[at(i + kWindow, j + kWindow)] - sum[at(i, j + kWindow)] -
                             sum[at(i + kWindow, j)] + sum[at(i, j)]);
        }
    }
    return counts;
}

int bestWindowCount(const LandMap& map, bool wantLand) {
    const std::vector<int> counts = windowCounts(map, wantLand);
    return *std::max_element(counts.begin(), counts.end());
}

constexpr std::array<std::array<int, 2>, 4> kNeighbors = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Number of samples of the largest 4-connected ocean region that does not touch the border.
int largestEnclosedOcean(const LandMap& map) {
    const int n = map.size;
    std::vector<std::uint8_t> seen(map.land.size(), 0);
    int largest = 0;
    for (int startJ = 0; startJ < n; ++startJ) {
        for (int startI = 0; startI < n; ++startI) {
            const auto start = static_cast<std::size_t>(startJ * n + startI);
            if (map.isLand(startI, startJ) || seen[start] != 0) {
                continue;
            }
            bool touchesBorder = false;
            int size = 0;
            std::vector<std::size_t> stack = {start};
            seen[start] = 1;
            while (!stack.empty()) {
                const std::size_t cell = stack.back();
                stack.pop_back();
                ++size;
                const int i = static_cast<int>(cell) % n;
                const int j = static_cast<int>(cell) / n;
                touchesBorder = touchesBorder || i == 0 || j == 0 || i == n - 1 || j == n - 1;
                for (const auto [di, dj] : kNeighbors) {
                    const int ni = i + di;
                    const int nj = j + dj;
                    const auto next = static_cast<std::size_t>(nj * n + ni);
                    if (ni >= 0 && nj >= 0 && ni < n && nj < n && !map.isLand(ni, nj) &&
                        seen[next] == 0) {
                        seen[next] = 1;
                        stack.push_back(next);
                    }
                }
            }
            if (!touchesBorder) {
                largest = std::max(largest, size);
            }
        }
    }
    return largest;
}

// Land fraction of each tile x tile block of samples.
std::vector<double> tileLandFractions(const LandMap& map, int tile) {
    std::vector<double> fractions;
    for (int tj = 0; tj + tile <= map.size; tj += tile) {
        for (int ti = 0; ti + tile <= map.size; ti += tile) {
            int land = 0;
            for (int j = tj; j < tj + tile; ++j) {
                for (int i = ti; i < ti + tile; ++i) {
                    land += map.isLand(i, j) ? 1 : 0;
                }
            }
            fractions.push_back(static_cast<double>(land) / (tile * tile));
        }
    }
    return fractions;
}

double standardDeviation(const std::vector<double>& values) {
    const auto count = static_cast<double>(values.size());
    double sum = 0.0;
    double squares = 0.0;
    for (const double v : values) {
        sum += v;
        squares += v * v;
    }
    return std::sqrt(std::max(0.0, squares / count - (sum / count) * (sum / count)));
}

// Fraction of the samples that are of the same kind (land or ocean) in both maps.
double sameKindFraction(const LandMap& a, const LandMap& b) {
    int same = 0;
    for (std::size_t k = 0; k < a.land.size(); ++k) {
        same += a.land[k] == b.land[k] ? 1 : 0;
    }
    return static_cast<double>(same) / static_cast<double>(a.land.size());
}

// ---------------------------------------------------------------------------

void testDeterminism() {
    const Heightfield a(kSeeds[0], macroParams(kAmplitude));
    const Heightfield b(kSeeds[0], macroParams(kAmplitude));
    const Heightfield other(kSeeds[1], macroParams(kAmplitude));
    Points points(20);
    std::vector<std::array<double, 2>> coordinates;
    for (int i = 0; i < 2000; ++i) {
        const double range = i % 3 == 0 ? 1.0e5 : (i % 3 == 1 ? 1.0e7 : 4.0e7);
        const double x = points.next(range);
        const double z = points.next(range);
        coordinates.push_back({x, z});
    }
    std::vector<HeightSample> forward;
    for (const auto& [x, z] : coordinates) {
        forward.push_back(a.sampleAt(x, z));
    }
    // Same samples from another instance, visited in reverse order.
    bool identical = true;
    int different = 0;
    for (std::size_t k = coordinates.size(); k-- > 0;) {
        const auto [x, z] = coordinates[k];
        const HeightSample s = b.sampleAt(x, z);
        identical = identical && s.height == forward[k].height &&
                    s.continentalness == forward[k].continentalness &&
                    s.macroContinentalness == forward[k].macroContinentalness;
        if (other.sampleAt(x, z).macroContinentalness != forward[k].macroContinentalness) {
            ++different;
        }
    }
    TEST_CHECK(identical);
    TEST_CHECK(different > static_cast<int>(coordinates.size()) * 9 / 10);
}

// The macro term is added to the local layer: continentalness = clamp(local + amplitude *
// tanh(macro / width), -1, 1), and the local layer is untouched when the macro layer is
// disabled.
void testLocalLayerKept() {
    constexpr float kTolerance = 1.0e-6f;
    constexpr int kCount = 2000;
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield on(seed, macroParams(kAmplitude));
        const Heightfield off(seed, macroParams(0.0f));
        // Layer 0 of the heightfield seeds is the local continentalness, fbm with 4 octaves.
        const std::uint64_t continentSeed = terrain::noise::deriveSeed(seed, 0);
        const terrain::noise::FbmParams local{
            .octaves = 4, .frequency = 1.0 / off.params().continentWavelength};
        Points points(seed + 30);
        bool kept = true;
        bool disabledIsLocal = true;
        int shifted = 0;
        for (int i = 0; i < kCount; ++i) {
            const double x = points.next(2.0e7);
            const double z = points.next(2.0e7);
            const HeightSample withMacro = on.sampleAt(x, z);
            const HeightSample without = off.sampleAt(x, z);
            const float term = kAmplitude * std::tanh(withMacro.macroContinentalness / kWidth);
            const float expected = std::clamp(without.continentalness + term, -1.0f, 1.0f);
            kept = kept && std::abs(withMacro.continentalness - expected) <= kTolerance;
            disabledIsLocal =
                disabledIsLocal && without.macroContinentalness == 0.0f &&
                without.continentalness ==
                    terrain::noise::fbm2D(x, z, continentSeed, local, terrain::WorldSize{});
            shifted += withMacro.continentalness != without.continentalness ? 1 : 0;
        }
        TEST_CHECK(kept);
        TEST_CHECK(disabledIsLocal);
        TEST_CHECK(shifted > kCount * 9 / 10); // the macro layer really contributes
        // Like every fbm layer, the macro layer is 0 at the origin: a macro coast.
        TEST_CHECK(on.sampleAt(0.0, 0.0).macroContinentalness == 0.0f);
    }
}

// One point and its three 1 cm neighbors: the height and the macro layer stay continuous.
bool continuousAt(const Heightfield& field, double x, double z) {
    // Measured slopes stay below 3.5 (far points and lattice lines, every seed); the bound
    // leaves a wide margin but still catches any jump larger than a few centimeters. The
    // macro layer moves by at most one float ulp (6e-8) over 1 cm.
    constexpr double kMaxSlope = 10.0;
    constexpr double kStep = 0.01;
    constexpr double kTolerance = kMaxSlope * kStep + 1.0e-3; // + float rounding
    constexpr float kMacroTolerance = 1.0e-6f;
    const HeightSample s = field.sampleAt(x, z);
    const HeightSample px = field.sampleAt(x + kStep, z);
    const HeightSample pz = field.sampleAt(x, z + kStep);
    const HeightSample diagonal = field.sampleAt(x - kStep, z - kStep);
    return std::isfinite(s.height) && std::abs(px.height - s.height) <= kTolerance &&
           std::abs(pz.height - s.height) <= kTolerance &&
           std::abs(diagonal.height - s.height) <= 2.0 * kTolerance &&
           std::abs(px.macroContinentalness - s.macroContinentalness) <= kMacroTolerance &&
           std::abs(pz.macroContinentalness - s.macroContinentalness) <= kMacroTolerance &&
           std::abs(diagonal.macroContinentalness - s.macroContinentalness) <= kMacroTolerance;
}

// Far from the origin and across the lattice lines of the macro layer (x = k * wavelength
// and z = k * wavelength), where a badly joined noise would jump.
void testContinuity() {
    constexpr double kLatticeOffset = 1.0e-3;
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield field(seed, macroParams(kAmplitude));
        Points points(seed + 50);
        bool continuous = true;
        for (int i = 0; i < 20000; ++i) {
            const double x = points.next(2.0e7);
            const double z = points.next(2.0e7);
            continuous = continuous && continuousAt(field, x, z);
        }
        bool onLattice = true;
        for (int k = -3; k <= 3; ++k) {
            for (const double offset : {-kLatticeOffset, kLatticeOffset}) {
                for (int i = 0; i < 300; ++i) {
                    const double line = k * kWavelength + offset;
                    const double along = points.next(2.0e7);
                    onLattice = onLattice && continuousAt(field, line, along) &&
                                continuousAt(field, along, line);
                }
            }
        }
        TEST_CHECK(continuous);
        TEST_CHECK(onLattice);
    }
}

// The macro layer makes oceans much larger than the 60 km local scale: a 2048 km window
// that is at least 95% ocean exists on the planet grid, and none without the layer.
void testVeryLargeOceans(const Planet& planet) {
    const int on = bestWindowCount(planet.onMap, false);
    const int off = bestWindowCount(planet.offMap, false);
    TEST_CHECK(on >= kPureCells);
    TEST_CHECK(off < kPureCells);
}

// Largest sea surrounded by land in the central 1024 km (128 x 128 samples of 8 km) of the
// 2048 km windows that are at least 95% land, scanned every 512 km in reading order. Stops
// at the first window whose sea reaches minCells samples.
int inlandSeaInContinents(const Planet& planet, int minCells) {
    constexpr int kScanStride = 4;
    constexpr int kSeaSamples = 128;
    constexpr double kSeaStep = 8000.0;
    const std::vector<int> counts = windowCounts(planet.onMap, true);
    const auto center = [](int first) {
        return (first + kWindow / 2.0 - kPlanetSize / 2.0) * kPlanetStep;
    };
    int largest = 0;
    for (int j0 = 0; j0 + kWindow <= kPlanetSize; j0 += kScanStride) {
        for (int i0 = 0; i0 + kWindow <= kPlanetSize; i0 += kScanStride) {
            if (counts[static_cast<std::size_t>(j0 * kWindowsPerRow + i0)] < kPureCells) {
                continue;
            }
            const LandMap window =
                sampleLandMap(planet.on, kSeaSamples, kSeaStep, center(i0), center(j0));
            largest = std::max(largest, largestEnclosedOcean(window));
            if (largest >= minCells) {
                return largest;
            }
        }
    }
    return largest;
}

// Same for land, and such continents hold inland seas of at least 16 samples of 8 km
// (1000 km^2). Not every continent does: the very interior of the largest ones (100% land on
// the planet grid) is solid land, so this looks for a window that does, among all of them.
// The largest inland seas of these windows measure 35 to 115 samples on the four seeds.
void testVeryLargeLand(const Planet& planet) {
    constexpr int kMinSeaCells = 16;
    const int on = bestWindowCount(planet.onMap, true);
    const int off = bestWindowCount(planet.offMap, true);
    const int sea = inlandSeaInContinents(planet, kMinSeaCells);
    TEST_CHECK(on >= kPureCells);
    TEST_CHECK(off < kPureCells);
    TEST_CHECK(sea >= kMinSeaCells);
}

// Over 60 km, the macro layer hardly moves, whereas the local layer varies a lot.
void testMacroVariesSlowly(const Planet& planet, std::uint64_t seed) {
    constexpr double kShift = 60000.0;
    constexpr int kCount = 10000;
    // Over 60 km, the macro layer changes by 0.014 to 0.016 at most and by 0.004 on average,
    // against 0.22 on average for the local layer (ratio 0.018 to 0.019 on every seed).
    constexpr float kMaxMacroChange = 0.03f;
    Points points(seed + 40);
    float maxMacro = 0.0f;
    double sumMacro = 0.0;
    double sumLocal = 0.0;
    for (int i = 0; i < kCount; ++i) {
        const double x = points.next(2.0e7);
        const double z = points.next(2.0e7);
        const double dx = i % 2 == 0 ? kShift : 0.0;
        const double dz = kShift - dx;
        const float macro = std::abs(planet.on.sampleAt(x + dx, z + dz).macroContinentalness -
                                     planet.on.sampleAt(x, z).macroContinentalness);
        const float local = std::abs(planet.off.sampleAt(x + dx, z + dz).continentalness -
                                     planet.off.sampleAt(x, z).continentalness);
        maxMacro = std::max(maxMacro, macro);
        sumMacro += macro;
        sumLocal += local;
    }
    TEST_CHECK(maxMacro < kMaxMacroChange);
    TEST_CHECK(sumMacro < 0.1 * sumLocal);
}

// Land fraction of the 1024 km tiles (8 x 8 planet samples): nearly the same everywhere
// without the macro layer, from no land to only land with it.
void testRegionalLandFraction(const Planet& planet) {
    constexpr int kTile = 8;
    const std::vector<double> with = tileLandFractions(planet.onMap, kTile);
    const std::vector<double> without = tileLandFractions(planet.offMap, kTile);
    const auto [lowest, highest] = std::minmax_element(with.begin(), with.end());
    const auto [lowestOff, highestOff] = std::minmax_element(without.begin(), without.end());
    const double spread = standardDeviation(with);
    const double spreadOff = standardDeviation(without);
    // Measured over the 1024 tiles of every seed: standard deviation 0.44 to 0.45 with the
    // macro layer against 0.062 without (7 times more); fractions from 0.00 to 1.00 with it,
    // from 0.28 to 0.70 without. The bounds leave a wide margin.
    TEST_CHECK(spread >= 3.0 * spreadOff);
    TEST_CHECK(*lowest < 0.1 && *highest > 0.9);
    TEST_CHECK(*lowestOff >= 0.1 && *highestOff <= 0.9);
}

// A 512 km window around the origin (on the macro coast, where the local layer decides) is
// no copy of its translations by multiples of the 60 km local wavelength or by the macro
// wavelength: a copy would have the same land and ocean at 100% of the samples.
void testNoTranslatedCopy(const Planet& planet) {
    constexpr int kSamples = 128;
    constexpr double kStep = 4000.0;
    // Measured fractions of identical samples: 0.45 to 0.57 on every seed and shift (about
    // 0.5 for unrelated windows). A copy would give 1.
    constexpr double kMaxSameKind = 0.8;
    constexpr std::array<std::array<double, 2>, 6> kShifts = {{{60000.0, 0.0},
                                                               {120000.0, 0.0},
                                                               {180000.0, 0.0},
                                                               {240000.0, 0.0},
                                                               {kWavelength, 0.0},
                                                               {0.0, kWavelength}}};
    const LandMap original = sampleLandMap(planet.on, kSamples, kStep, 0.0, 0.0);
    for (const auto& [dx, dz] : kShifts) {
        const LandMap moved = sampleLandMap(planet.on, kSamples, kStep, dx, dz);
        TEST_CHECK(sameKindFraction(original, moved) < kMaxSameKind);
    }
}

} // namespace

int main() {
    testDeterminism();
    testLocalLayerKept();
    testContinuity();
    for (const std::uint64_t seed : kSeeds) {
        const Planet planet = makePlanet(seed);
        testVeryLargeOceans(planet);
        testVeryLargeLand(planet);
        testMacroVariesSlowly(planet, seed);
        testRegionalLandFraction(planet);
        testNoTranslatedCopy(planet);
    }
    return test::result();
}
