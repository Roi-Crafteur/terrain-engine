#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

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

// Coarse grid of kGridSize x kGridSize samples, kGridStep meters apart, around the origin:
// a 128 km square, several times the continent and mountain mask wavelengths.
constexpr int kGridSize = 256;
constexpr double kGridStep = 500.0;

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

struct Grid {
    std::vector<HeightSample> samples; // row by row

    [[nodiscard]] const HeightSample& at(int i, int j) const {
        return samples[static_cast<std::size_t>(j * kGridSize + i)];
    }
};

double gridCoordinate(int index) {
    return (index - kGridSize / 2) * kGridStep;
}

Grid sampleGrid(const Heightfield& field) {
    Grid grid;
    grid.samples.reserve(static_cast<std::size_t>(kGridSize * kGridSize));
    for (int j = 0; j < kGridSize; ++j) {
        for (int i = 0; i < kGridSize; ++i) {
            grid.samples.push_back(field.sampleAt(gridCoordinate(i), gridCoordinate(j)));
        }
    }
    return grid;
}

// ---------------------------------------------------------------------------

void testDeterminism() {
    const Heightfield a(kSeeds[0]);
    const Heightfield b(kSeeds[0]);
    const Heightfield other(kSeeds[1]);
    Points points(10);
    std::vector<std::array<double, 2>> coordinates;
    for (int i = 0; i < 2000; ++i) {
        const double range = i % 2 == 0 ? 1.0e5 : 1.0e7;
        coordinates.push_back({points.next(range), points.next(range)});
    }
    std::vector<float> forward;
    for (const auto& [x, z] : coordinates) {
        forward.push_back(a.heightAt(x, z));
    }
    // Same heights from another instance, visited in reverse order.
    bool identical = true;
    int different = 0;
    for (std::size_t k = coordinates.size(); k-- > 0;) {
        const auto [x, z] = coordinates[k];
        identical = identical && b.heightAt(x, z) == forward[k] &&
                    a.sampleAt(x, z).height == forward[k];
        if (other.heightAt(x, z) != forward[k]) {
            ++different;
        }
    }
    TEST_CHECK(identical);
    TEST_CHECK(different > static_cast<int>(coordinates.size()) * 9 / 10);
}

void testLocalContinuity() {
    // The walls of the mountains rise at up to 89 degrees (a slope of about 90 over a few
    // meters, the same at 1 cm and at 1 mm): the bound leaves a margin and, over 1 mm, still
    // catches any jump larger than a few centimeters.
    constexpr double kMaxSlope = 100.0;
    constexpr double kStep = 0.001;
    constexpr double kTolerance = kMaxSlope * kStep + 1.0e-3; // + float rounding
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield field(seed);
        Points points(seed);
        bool continuous = true;
        for (int i = 0; i < 20000; ++i) {
            const double range = i % 4 == 0 ? 1.0e6 : 6.0e4;
            const double x = points.next(range);
            const double z = points.next(range);
            const float h = field.heightAt(x, z);
            continuous = continuous && std::isfinite(h) &&
                         std::abs(field.heightAt(x + kStep, z) - h) <= kTolerance &&
                         std::abs(field.heightAt(x, z + kStep) - h) <= kTolerance &&
                         std::abs(field.heightAt(x - kStep, z - kStep) - h) <= 2.0 * kTolerance;
        }
        TEST_CHECK(continuous);
    }
}

// The ground is below sea level exactly where the continentalness is below the coast:
// mountains only rise on land and the detail fades out near sea level.
void testOceansOnlyAtLowContinentalness(const Grid& grid, const HeightfieldParams& params) {
    int ocean = 0;
    bool consistent = true;
    for (const HeightSample& s : grid.samples) {
        const bool lowContinentalness = s.continentalness - params.coastContinentalness < 0.0f;
        consistent = consistent && (s.height < kSeaLevel) == lowContinentalness;
        ocean += s.height < kSeaLevel ? 1 : 0;
    }
    TEST_CHECK(consistent);
    // The grid holds both oceans and continents.
    const auto total = static_cast<int>(grid.samples.size());
    TEST_CHECK(ocean > total / 10);
    TEST_CHECK(ocean < total * 9 / 10);
}

constexpr std::array<std::array<int, 2>, 4> kNeighbors = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Sizes of the 4-connected ocean regions that do not touch the grid border.
std::vector<int> enclosedOceanSizes(const Grid& grid) {
    std::vector<int> label(grid.samples.size(), -1);
    std::vector<int> sizes;
    for (std::size_t start = 0; start < grid.samples.size(); ++start) {
        if (grid.samples[start].height >= kSeaLevel || label[start] >= 0) {
            continue;
        }
        bool touchesBorder = false;
        int size = 0;
        std::vector<std::size_t> stack = {start};
        label[start] = 0;
        while (!stack.empty()) {
            const std::size_t cell = stack.back();
            stack.pop_back();
            ++size;
            const int i = static_cast<int>(cell) % kGridSize;
            const int j = static_cast<int>(cell) / kGridSize;
            touchesBorder = touchesBorder || i == 0 || j == 0 || i == kGridSize - 1 ||
                            j == kGridSize - 1;
            for (const auto [di, dj] : kNeighbors) {
                const int ni = i + di;
                const int nj = j + dj;
                if (ni < 0 || nj < 0 || ni >= kGridSize || nj >= kGridSize) {
                    continue;
                }
                const auto next = static_cast<std::size_t>(nj * kGridSize + ni);
                if (grid.samples[next].height < kSeaLevel && label[next] < 0) {
                    label[next] = 0;
                    stack.push_back(next);
                }
            }
        }
        if (!touchesBorder) {
            sizes.push_back(size);
        }
    }
    return sizes;
}

// Oceans are continuous bodies of water: no isolated puddle below sea level. An enclosed
// ocean region (inland sea or lake) covers at least 1 km^2, i.e. 4 grid cells.
void testOceansContinuous(const Grid& grid) {
    constexpr int kMinCells = 4;
    bool noPuddle = true;
    for (const int size : enclosedOceanSizes(grid)) {
        noPuddle = noPuddle && size >= kMinCells;
    }
    TEST_CHECK(noPuddle);
}

void testMountainsOnlyInMask(const Grid& grid, const HeightfieldParams& params) {
    constexpr float kRounding = 1.0e-3f;
    const float maxWithoutMountain = terrain::kMaxBaseHeight + params.detailAmplitude;
    bool inMask = true;
    float highest = 0.0f;
    for (const HeightSample& s : grid.samples) {
        inMask = inMask && s.mountainMask >= 0.0f && s.mountainMask <= 1.0f &&
                 s.mountain >= 0.0f &&
                 s.mountain <= s.mountainMask * params.mountainHeight + kRounding &&
                 (s.mountainMask > 0.0f || s.mountain == 0.0f) &&
                 (s.height <= maxWithoutMountain + kRounding || s.mountainMask > 0.0f);
        highest = std::fmax(highest, s.mountain);
    }
    TEST_CHECK(inMask);
    // The grid really holds mountains.
    TEST_CHECK(highest > 1000.0f);
}

// Without the mask, nothing rises above the continental curve plus the light detail.
void testNoPeaksWithoutMask(const Grid& grid, const HeightfieldParams& params) {
    bool flat = true;
    int unmaskedLand = 0;
    for (const HeightSample& s : grid.samples) {
        if (s.mountainMask == 0.0f) {
            flat = flat && s.height <= s.base + params.detailAmplitude;
            unmaskedLand += s.height > kSeaLevel ? 1 : 0;
        }
    }
    TEST_CHECK(flat);
    TEST_CHECK(unmaskedLand > 0);
}

// Same world with a mask that is zero everywhere: no peak and gentle land slopes, whereas
// the legacy world has both.
void testNoPeaksWhenMaskIsEmpty(std::uint64_t seed) {
    HeightfieldParams noMask = test_relief::legacyRelief();
    noMask.maskLow = 2.0f; // fbm never reaches it
    noMask.maskHigh = 3.0f;
    const Heightfield flat(seed, noMask);
    const Heightfield normal(seed, test_relief::legacyRelief());

    constexpr double kStep = 5.0;
    constexpr double kMaxLandSlope = 0.5;
    Points points(seed + 100);
    bool gentle = true;
    double steepestNormal = 0.0;
    float highestFlat = kSeaLevel;
    for (int i = 0; i < 20000; ++i) {
        const double x = points.next(6.4e4);
        const double z = points.next(6.4e4);
        const HeightSample s = flat.sampleAt(x, z);
        gentle = gentle && s.mountainMask == 0.0f && s.mountain == 0.0f;
        highestFlat = std::fmax(highestFlat, s.height);
        if (s.height > kSeaLevel) {
            const double slope = std::abs(flat.heightAt(x + kStep, z) - s.height) / kStep;
            gentle = gentle && slope <= kMaxLandSlope;
        }
        const float h = normal.heightAt(x, z);
        steepestNormal =
            std::fmax(steepestNormal, std::abs(normal.heightAt(x + kStep, z) - h) / kStep);
    }
    TEST_CHECK(gentle);
    TEST_CHECK(highestFlat <= terrain::kMaxBaseHeight + noMask.detailAmplitude);
    TEST_CHECK(steepestNormal > kMaxLandSlope);
}

} // namespace

int main() {
    testDeterminism();
    testLocalContinuity();
    // The continentalness, the mask and the oceans of the legacy relief (the rules of the
    // planetary geography: geography_test).
    for (const std::uint64_t seed : kSeeds) {
        const Heightfield field(seed, test_relief::legacyRelief());
        const Grid grid = sampleGrid(field);
        testOceansOnlyAtLowContinentalness(grid, field.params());
        testOceansContinuous(grid);
        testMountainsOnlyInMask(grid, field.params());
        testNoPeaksWithoutMask(grid, field.params());
        testNoPeaksWhenMaskIsEmpty(seed);
    }
    return test::result();
}
