// Overhangs and arches of a region seen from the volume: determinism, overhangs and arches
// present, void under the arches, arches standing on rock, no floating rock, and continuity
// with the cliffs of Phase 5A. Rivers, cascades, cirques, oceans and coasts:
// sculpted_water_test.cpp. Every parameter is read from the fields, nothing is hard-coded.
#include "terrain/generation/sculpted_region.hpp"
#include "terrain/generation/noise.hpp"

#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

using terrain::Arch;
using terrain::ArchField;
using terrain::HeightmapRegion;
using terrain::SculptedColumn;
using terrain::SculptedRegion;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

struct Point {
    double x;
    double z;
};

// Deterministic pseudo-random numbers in [0, 1): the stream is a seed, each call a counter.
class Rng {
public:
    explicit Rng(std::uint64_t stream) : stream_(stream) {}

    double next() {
        const std::uint64_t bits = terrain::noise::deriveSeed(stream_, counter_++);
        return static_cast<double>(bits >> 40) / 16777216.0;
    }
    double range(double low, double high) { return low + (high - low) * next(); }

private:
    std::uint64_t stream_;
    std::uint64_t counter_ = 0;
};

// Random point of the square `scale` times as large as the region, around its center.
Point randomPoint(const HeightmapRegion& region, Rng& rng, double scale) {
    const double half = scale * region.size * region.metersPerPixel / 2.0;
    return {region.centerX + rng.range(-half, half), region.centerZ + rng.range(-half, half)};
}

// World position at (along, across) from the center of an arch.
Point at(const Arch& arch, double along, double across) {
    return {arch.x + along * arch.directionX - across * arch.directionZ,
            arch.z + along * arch.directionZ + across * arch.directionX};
}

bool isPlain(const SculptedColumn& column) {
    return column.overhang == 0.0f && column.arch == terrain::kNoArch;
}

// Samples of the grids where rock lies above air.
std::vector<std::size_t> overhangSamples(const SculptedRegion& region) {
    std::vector<std::size_t> samples;
    for (std::size_t i = 0; i < region.airBelowRock().size(); ++i) {
        if (region.airBelowRock()[i] != 0) {
            samples.push_back(i);
        }
    }
    return samples;
}

Point samplePoint(const SculptedRegion& region, std::size_t index) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    const int size = eroded.grid().size;
    return {eroded.sampleX(static_cast<int>(index) % size),
            eroded.sampleZ(static_cast<int>(index) / size)};
}

// ---------------------------------------------------------------------------

// Same seed, same region: same arches, grids and densities. Another seed: another world.
void testDeterminism(const SculptedRegion& a, const HeightmapRegion& site) {
    const SculptedRegion b(kSeed, site,
                           test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    TEST_CHECK(a.arches() == b.arches());
    TEST_CHECK(a.controlMask() == b.controlMask());
    TEST_CHECK(a.overhangWeight() == b.overhangWeight());
    TEST_CHECK(a.surface().heights == b.surface().heights);
    TEST_CHECK(a.airBelowRock() == b.airBelowRock());
    Rng rng(3);
    bool same = true;
    for (int k = 0; k < 600; ++k) {
        const Point p = randomPoint(site, rng, 1.0);
        const double y = a.cliffs().eroded().heightAt(p.x, p.z) + rng.range(-60.0, 60.0);
        same = same && a.densityAt(p.x, y, p.z) == b.densityAt(p.x, y, p.z);
    }
    TEST_CHECK(same);

    const HeightmapRegion small =
        terrain::findCliffRegion(kSeed, 100.0, 48, {}, test_relief::legacyRelief());
    const SculptedRegion c(kSeed, small,
                           test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    const SculptedRegion d(kSeed + 1, small,
                           test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    TEST_CHECK(c.controlMask() != d.controlMask() || !(c.arches() == d.arches()));
}

// Overhangs exist, only where the weight or an arch puts them, and some banks jut out above
// the lower treads of the cliffs.
void testOverhangsPresent(const SculptedRegion& region) {
    const std::vector<std::size_t> samples = overhangSamples(region);
    bool explained = true;
    int overhangs = 0;
    for (const std::size_t i : samples) {
        const Point p = samplePoint(region, i);
        const SculptedColumn column = region.columnAt(p.x, p.z);
        explained = explained && !isPlain(column) && region.overhangWeight()[i] == column.overhang;
        overhangs += column.overhang > 0.0f && column.arch == terrain::kNoArch ? 1 : 0;
    }
    int banks = 0;
    for (std::size_t i = 0; i < region.surface().heights.size(); ++i) {
        banks += region.surface().heights[i] > region.cliffs().surface().heights[i] + 1.0f ? 1 : 0;
    }
    std::printf("overhangs: %zu samples with rock above air (%d under overhangs), %d banks "
                "above a lower tread\n",
                samples.size(), overhangs, banks);
    TEST_CHECK(explained);
    TEST_CHECK(overhangs >= 20);
    TEST_CHECK(banks >= 5);
}

// Arches exist and follow the placement rules: inside the region, in full cliffs, on level
// rock, apart from each other.
void testArchPlacement(const SculptedRegion& region) {
    const ArchField& field = region.archField();
    const terrain::ArchParams& p = field.params();
    const terrain::CliffRegion& cliffs = region.cliffs();
    const HeightmapRegion& area = cliffs.eroded().region();
    const double half = area.size * area.metersPerPixel / 2.0;
    std::printf("arches: %zu\n", region.arches().size());
    TEST_CHECK(region.arches().size() >= 3);
    bool valid = true;
    for (std::size_t i = 0; i < region.arches().size(); ++i) {
        const Arch& arch = region.arches()[i];
        const double reach = field.reach(arch);
        valid = valid && std::abs(arch.x - area.centerX) + reach < half &&
                std::abs(arch.z - area.centerZ) + reach < half &&
                cliffs.columnAt(arch.x, arch.z).cliff >= p.minCliffMask;
        for (const double along : {-1.0, 0.0, 1.0}) {
            const Point foot = at(arch, along * arch.halfSpan, 0.0);
            const float surface = cliffs.surfaceAt(foot.x, foot.z);
            valid = valid && surface >= arch.base - 1.0e-3f &&
                    surface <= arch.base + p.maxGroundRelief + 1.0e-3f;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const Arch& other = region.arches()[j];
            valid = valid && std::hypot(other.x - arch.x, other.z - arch.z) >=
                                 reach + field.reach(other);
        }
    }
    TEST_CHECK(valid);
}

// Under each arch, from its base up to the half-disk of its opening and across its tube,
// there is only air; the crown is rock, with air above it.
void testVoidUnderArches(const SculptedRegion& region) {
    const ArchField& field = region.archField();
    const terrain::ArchParams& p = field.params();
    int checked = 0;
    bool open = true;
    bool crowned = true;
    for (const Arch& arch : region.arches()) {
        const double inner = arch.halfSpan - p.pillarRadius;
        for (const double along : {-0.8 * inner, 0.0, 0.8 * inner}) {
            for (const double across : {-0.8 * p.tubeRadius, 0.0, 0.8 * p.tubeRadius}) {
                const Point under = at(arch, along, across);
                const SculptedColumn column = region.columnAt(under.x, under.z);
                for (double y = arch.base + 0.25; y < field.openingTop(arch) - 0.25; y += 0.5) {
                    // Inside the half-disk of the opening above the springline.
                    const double rise = y - field.springline(arch);
                    if (rise > 0.0 && std::hypot(along, rise) >= field.openingTop(arch) -
                                                                     field.springline(arch) - 0.25) {
                        continue;
                    }
                    ++checked;
                    open = open && region.density(column, y) > 0.0f;
                }
            }
        }
        const Point center = at(arch, 0.0, 0.0);
        const SculptedColumn column = region.columnAt(center.x, center.z);
        crowned = crowned &&
                  region.density(column, field.springline(arch) + arch.halfSpan) < 0.0f &&
                  region.density(column, field.crown(arch) + 0.25) > 0.0f &&
                  region.scan(column).airBelowRock;
    }
    TEST_CHECK(checked > 1000);
    TEST_CHECK(open);
    TEST_CHECK(crowned);
}

// The pillars are solid from the rock below the base up to the springline, and the base is
// the rock surface of the cliffs: the arches stand on the rock.
void testArchesStandOnRock(const SculptedRegion& region) {
    const ArchField& field = region.archField();
    const terrain::ArchParams& p = field.params();
    bool solid = true;
    bool onRock = true;
    for (const Arch& arch : region.arches()) {
        for (const double side : {-1.0, 1.0}) {
            const Point pillar = at(arch, side * arch.halfSpan, 0.0);
            const SculptedColumn column = region.columnAt(pillar.x, pillar.z);
            for (double y = arch.base - p.anchorDepth + 0.25; y < field.springline(arch);
                 y += 0.5) {
                solid = solid && region.density(column, y) < 0.0f;
            }
            onRock = onRock && region.cliffs().densityAt(pillar.x, arch.base - 0.25, pillar.z) < 0.0f;
        }
    }
    TEST_CHECK(solid);
    TEST_CHECK(onRock);
}

// A block of the volume sampled every 2 m, columns first.
struct Block {
    Point center;
    double half;   // horizontal half-extent
    double bottom; // heights
    double top;
};

constexpr double kVoxel = 2.0;

// Number of rock voxels of the block not connected (6-neighborhood) to its faces: floating
// rock. A cantilever reaching a face counts as held.
int floatingVoxels(const SculptedRegion& region, const Block& block) {
    const int n = static_cast<int>(std::ceil(2.0 * block.half / kVoxel)) + 1;
    const int h = static_cast<int>(std::ceil((block.top - block.bottom) / kVoxel)) + 1;
    std::vector<char> rock(static_cast<std::size_t>(n * n * h), 0);
    const auto index = [&](int i, int j, int k) {
        return static_cast<std::size_t>((k * n + j) * h + i);
    };
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            const SculptedColumn column =
                region.columnAt(block.center.x - block.half + j * kVoxel,
                                block.center.z - block.half + k * kVoxel);
            for (int i = 0; i < h; ++i) {
                rock[index(i, j, k)] = region.density(column, block.bottom + i * kVoxel) < 0.0f;
            }
        }
    }
    std::deque<std::array<int, 3>> queue;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < h; ++i) {
                const bool face = i == 0 || j == 0 || k == 0 || i == h - 1 || j == n - 1 || k == n - 1;
                if (face && rock[index(i, j, k)] == 1) {
                    rock[index(i, j, k)] = 2;
                    queue.push_back({i, j, k});
                }
            }
        }
    }
    constexpr int kSteps[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    while (!queue.empty()) {
        const auto [i, j, k] = queue.front();
        queue.pop_front();
        for (const auto& step : kSteps) {
            const int a = i + step[0];
            const int b = j + step[1];
            const int c = k + step[2];
            if (a >= 0 && b >= 0 && c >= 0 && a < h && b < n && c < n && rock[index(a, b, c)] == 1) {
                rock[index(a, b, c)] = 2;
                queue.push_back({a, b, c});
            }
        }
    }
    return static_cast<int>(std::count(rock.begin(), rock.end(), char{1}));
}

// No rock floats: around arches and overhangs every rock voxel is held by the faces of its
// block (by the ground below or by the rock around).
void testNoFloatingRock(const SculptedRegion& region) {
    const ArchField& field = region.archField();
    std::vector<Block> blocks;
    for (std::size_t a = 0; a < region.arches().size() && a < 10; ++a) {
        const Arch& arch = region.arches()[a];
        blocks.push_back({{arch.x, arch.z}, field.boundingRadius(arch) + 6.0,
                          arch.base - field.params().anchorDepth - 4.0, field.crown(arch) + 4.0});
    }
    const std::vector<std::size_t> samples = overhangSamples(region);
    const std::size_t stride = std::max<std::size_t>(1, samples.size() / 20);
    for (std::size_t s = 0; s < samples.size(); s += stride) {
        const float top = region.surface().heights[samples[s]];
        blocks.push_back({samplePoint(region, samples[s]), 32.0,
                          top - region.cliffs().field().params().stratumHeight - 8.0, top + 6.0});
    }
    int floating = 0;
    for (const Block& block : blocks) {
        floating += floatingVoxels(region, block);
    }
    std::printf("floating rock: %d voxels in %zu blocks\n", floating, blocks.size());
    TEST_CHECK(blocks.size() >= 10);
    TEST_CHECK(floating == 0);
}

// Wherever a column has no overhang and no arch within reach, and beyond the region, the
// density is exactly the density of the cliffs (Phase 5A), and so is the surface.
void testExactCliffsWherePlain(const SculptedRegion& region) {
    const HeightmapRegion& area = region.cliffs().eroded().region();
    Rng rng(7);
    int plain = 0;
    bool exact = true;
    for (int k = 0; k < 3000; ++k) {
        const Point p = randomPoint(area, rng, 1.3);
        const SculptedColumn column = region.columnAt(p.x, p.z);
        if (!isPlain(column)) {
            continue;
        }
        ++plain;
        const float ground = column.cliff.ground;
        for (const double offset : {-200.0, -45.0, -12.0, -0.5, 0.0, 3.0, 80.0}) {
            const double y = static_cast<double>(ground) + offset;
            exact = exact && region.density(column, y) == region.cliffs().densityAt(p.x, y, p.z);
        }
    }
    bool grids = true;
    for (std::size_t i = 0; i < region.surface().heights.size(); ++i) {
        const Point p = samplePoint(region, i);
        if (isPlain(region.columnAt(p.x, p.z))) {
            grids = grids && region.surface().heights[i] == region.cliffs().surface().heights[i] &&
                    region.airBelowRock()[i] == 0;
        }
    }
    std::printf("plain columns: %d of 3000 points\n", plain);
    TEST_CHECK(plain > 1000);
    TEST_CHECK(exact);
    TEST_CHECK(grids);
}

// No jump of the density near the surfaces, around the arches and the overhangs included: a
// step of 1 mm along x, y or z changes it by at most kMaxSlope * step. The cliffs need 60;
// the overhangs add the slopes of their profile and of their weight times their size.
void testNoJumpNearSurface(const SculptedRegion& region) {
    constexpr double kStep = 1.0e-3;
    constexpr double kMaxSlope = 100.0;
    constexpr double kSlack = 2.0e-3;
    const HeightmapRegion& area = region.cliffs().eroded().region();
    std::vector<Point> points;
    Rng rng(11);
    for (int k = 0; k < 4000; ++k) {
        points.push_back(randomPoint(area, rng, 1.1));
    }
    for (const std::size_t i : overhangSamples(region)) {
        const Point p = samplePoint(region, i);
        points.push_back({p.x + rng.range(-10.0, 10.0), p.z + rng.range(-10.0, 10.0)});
    }
    for (const Arch& arch : region.arches()) {
        for (int k = 0; k < 40; ++k) {
            points.push_back(at(arch, rng.range(-40.0, 40.0), rng.range(-10.0, 10.0)));
        }
    }
    double worst = 0.0;
    bool continuous = true;
    for (const Point& p : points) {
        const double y = region.surfaceAt(p.x, p.z) + rng.range(-10.0, 10.0);
        const float here = region.densityAt(p.x, y, p.z);
        const float shifted[3] = {region.densityAt(p.x + kStep, y, p.z),
                                  region.densityAt(p.x, y + kStep, p.z),
                                  region.densityAt(p.x, y, p.z + kStep)};
        for (const float value : shifted) {
            const double change = std::abs(static_cast<double>(value) - static_cast<double>(here));
            worst = std::max(worst, change / kStep);
            continuous = continuous && std::isfinite(value) && change <= kMaxSlope * kStep + kSlack;
        }
    }
    std::printf("no jump: largest |change| / step = %.2f (limit %.0f) over %zu points\n", worst,
                kMaxSlope, points.size());
    TEST_CHECK(continuous);
}

// The chunk columns give the density of the region, between bounds that hold the surfaces.
void testChunkColumns(const SculptedRegion& region) {
    TEST_CHECK(!region.arches().empty());
    if (region.arches().empty()) {
        return;
    }
    const Arch& arch = region.arches().front();
    constexpr double kSpacing = 2.5;
    const double originX = arch.x - 8.0 * kSpacing;
    const double originZ = arch.z - 8.0 * kSpacing;
    const terrain::SculptedChunkColumns columns =
        terrain::sampleSculptedColumns(region, originX, originZ, kSpacing);
    const double originY = arch.base - 10.0;
    const terrain::DensityGrid grid = terrain::sculptedDensityFromColumns(region, columns, originY);
    bool same = true;
    bool bounded = true;
    for (int z = 0; z < terrain::kGridSamples; ++z) {
        for (int x = 0; x < terrain::kGridSamples; ++x) {
            const SculptedColumn& column = columns.at(x, z);
            const SculptedRegion::Bounds bounds = region.bounds(column);
            bounded = bounded && region.density(column, bounds.high) > 0.0f &&
                      region.density(column, bounds.low) < 0.0f &&
                      columns.minHeight <= bounds.low && columns.maxHeight >= bounds.high;
            for (int y = 0; y < terrain::kGridSamples; ++y) {
                same = same && grid.at(x, y, z) == region.densityAt(originX + x * kSpacing,
                                                                   originY + y * kSpacing,
                                                                   originZ + z * kSpacing);
            }
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(bounded);
}

} // namespace

int main() {
    const HeightmapRegion site =
        terrain::findCliffRegion(kSeed, 50.0, 129, {}, test_relief::legacyRelief());
    const SculptedRegion region(kSeed, site,
                                test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    testDeterminism(region, site);
    testOverhangsPresent(region);
    testArchPlacement(region);
    testVoidUnderArches(region);
    testArchesStandOnRock(region);
    testNoFloatingRock(region);
    testExactCliffsWherePlain(region);
    testNoJumpNearSurface(region);
    testChunkColumns(region);
    return test::result();
}
