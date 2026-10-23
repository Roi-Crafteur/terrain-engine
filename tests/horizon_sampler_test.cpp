// Samples of the far horizon: the ground of the point samples (crestRatio 0; horizon_crests_test
// tests the crests) is Heightfield::heightAt bit for bit, the material and the biome are those
// of Ecology::map(surfaceFromHeightfield(...)) at the same points bit for bit on every level
// (the oracle that guards the copy of the site in horizon_sampler.cpp), in an
// ocean, on a coast, in mountains, in a desert, at a pole and at the corner of the world of the
// legacy relief, and on a coast of the default world found by search; then
// the periodicity, the independence from the rectangle, the gradients, the summaries of the
// player edits and the invalid arguments.
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include "default_world_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::HorizonCell;
using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;
using terrain::HorizonSampler;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr double kOracleSpacing = 8.0; // the default materialSlopeStep
constexpr int kOracleSize = 32;

struct Region {
    const char* name;
    double x0; // first sample of the oracle, a multiple of 8 m
    double z0;
};

// The regions of the oracle, found once on a lattice of 65,536 m around the world with the
// default seed; each one starts 128 m before its point, a multiple of 4096 m (a sample of every
// level), so that the oracle holds samples of every level.
std::vector<Region> oracleRegions(const terrain::WorldSize& world) {
    return {
        {"ocean", -3'866'624.0 - 128.0, -3'932'160.0 - 128.0},
        {"coast", 282'624.0 - 128.0, -4'050'944.0 - 128.0},
        {"mountain", -1'441'792.0 - 128.0, -3'932'160.0 - 128.0},
        {"desert", -3'604'480.0 - 128.0, -3'932'160.0 - 128.0},
        {"pole", -3'604'480.0 - 128.0, -world.z / 2.0 - 128.0},
        {"world corner", world.x / 2.0 - 128.0, world.z / 2.0 - 128.0},
    };
}

terrain::EcologyMap oracleMap(const terrain::Heightfield& field, const terrain::Ecology& ecology,
                              const Region& region) {
    // Pixel i of the region lies at center + (i + 0.5 - size / 2) * spacing = x0 + 8 i.
    const double offset = (kOracleSize / 2.0 - 0.5) * kOracleSpacing;
    const terrain::HeightmapRegion area{region.x0 + offset, region.z0 + offset, kOracleSpacing,
                                        kOracleSize};
    return ecology.map(terrain::surfaceFromHeightfield(field, area));
}

// Samples of a level within [first, first + (size - 1) * spacing] along an axis.
std::pair<std::int64_t, std::int64_t> levelSpan(double first, int pitch) {
    const double last = first + (kOracleSize - 1) * kOracleSpacing;
    return {static_cast<std::int64_t>(std::ceil(first / pitch)),
            static_cast<std::int64_t>(std::floor(last / pitch)) + 1};
}

struct RegionStats {
    int compared = 0;
    int water = 0;
    int land = 0;
    float highest = -1.0e9f;
};

void compareLevel(const HorizonSampler& sampler, const terrain::EcologyMap& map,
                  const Region& region, int level, RegionStats& stats) {
    const int pitch = terrain::levelPitch(sampler.params(), level);
    const auto [x0, x1] = levelSpan(region.x0, pitch);
    const auto [z0, z1] = levelSpan(region.z0, pitch);
    const HorizonRect rect{x0, z0, x1, z1};
    const std::vector<HorizonCell> cells = sampler.sample(level, rect);
    std::size_t n = 0;
    for (std::int64_t k = z0; k < z1; ++k) {
        for (std::int64_t i = x0; i < x1; ++i) {
            const HorizonCell& cell = cells[n++];
            const double x = terrain::samplePosition(i, pitch);
            const double z = terrain::samplePosition(k, pitch);
            const auto column = static_cast<int>((x - region.x0) / kOracleSpacing);
            const auto row = static_cast<int>((z - region.z0) / kOracleSpacing);
            const terrain::EcologySample& expected = map.at(column, row);
            const float ground = sampler.heightfield().heightAt(x, z);
            TEST_CHECK(cell.height == ground);
            TEST_CHECK(cell.material == expected.material);
            TEST_CHECK(cell.biome == expected.biome);
            TEST_CHECK(((cell.flags & terrain::kHorizonWater) != 0) == (ground < 0.0f));
            TEST_CHECK((cell.flags & terrain::kHorizonEdited) == 0);
            ++stats.compared;
            ++((cell.flags & terrain::kHorizonWater) != 0 ? stats.water : stats.land);
            stats.highest = std::max(stats.highest, cell.height);
        }
    }
}

// The point samples (crestRatio 0): with crests, only the heights of the levels 1 and up change
// (horizon_crests_test).
HorizonParams pointSamples() {
    HorizonParams params;
    params.crestRatio = 0;
    return params;
}

void testMaterialOracle() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    const terrain::Ecology ecology(kSeed);
    const HorizonParams params = pointSamples();
    const HorizonSampler sampler(field, ecology, params);
    std::vector<RegionStats> allStats;
    for (const Region& region : oracleRegions(params.world)) {
        const terrain::EcologyMap map = oracleMap(field, ecology, region);
        RegionStats stats;
        for (int level = 0; level < params.levels; ++level) {
            compareLevel(sampler, map, region, level, stats);
        }
        std::printf("oracle %s: %d samples (%d water, %d land), highest %.1f m\n", region.name,
                    stats.compared, stats.water, stats.land, stats.highest);
        TEST_CHECK(stats.compared > kOracleSize * kOracleSize);
        allStats.push_back(stats);
    }
    // Each region holds what it is named after.
    const std::vector<Region> regions = oracleRegions(params.world);
    TEST_CHECK(allStats[0].land == 0);                             // ocean
    TEST_CHECK(allStats[1].water > 300 && allStats[1].land > 300); // coast
    TEST_CHECK(allStats[2].highest > 2000.0f);                     // mountain
    TEST_CHECK(oracleMap(field, ecology, regions[2]).at(16, 16).biome == terrain::Biome::Mountain);
    TEST_CHECK(oracleMap(field, ecology, regions[3]).at(16, 16).biome == terrain::Biome::Desert);
}

// A coast of the default world: the first point of a lattice of 4096 m (a sample of every level)
// around the origin whose oracle square, from 128 m before it, holds over 300 samples of water
// and as many of land. The oracle holds there too.
void testDefaultWorldCoast() {
    const terrain::Heightfield& field = default_world::field();
    const auto mixed = [&field](double x, double z) {
        int water = 0;
        for (int j = 0; j < kOracleSize; ++j) {
            for (int i = 0; i < kOracleSize; ++i) {
                water += field.heightAt(x - 128.0 + i * kOracleSpacing,
                                        z - 128.0 + j * kOracleSpacing) < 0.0f
                             ? 1
                             : 0;
            }
        }
        return water > 300 && water < kOracleSize * kOracleSize - 300;
    };
    const std::optional<default_world::Place> coast = default_world::nearest(4096.0, 64, mixed);
    TEST_CHECK(coast.has_value());
    if (!coast) {
        return;
    }
    const terrain::Ecology ecology(kSeed);
    const HorizonParams params = pointSamples();
    const HorizonSampler sampler(field, ecology, params);
    const Region region{"default world coast", coast->x - 128.0, coast->z - 128.0};
    const terrain::EcologyMap map = oracleMap(field, ecology, region);
    RegionStats stats;
    for (int level = 0; level < params.levels; ++level) {
        compareLevel(sampler, map, region, level, stats);
    }
    std::printf("oracle %s at (%.0f, %.0f) m: %d samples (%d water, %d land)\n", region.name,
                coast->x, coast->z, stats.compared, stats.water, stats.land);
    TEST_CHECK(stats.water > 300 && stats.land > 300);
}

// The same samples for every image of a rectangle, and whatever rectangle holds them.
void testPeriodicAndSplit() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    const terrain::Ecology ecology(kSeed);
    const HorizonParams params;
    const HorizonSampler sampler(field, ecology, params);
    const int level = 2;
    const HorizonIndex counts = terrain::levelCounts(params, level);
    const HorizonIndex center = terrain::canonicalCenter(params, level, -1'441'792.0, -3'932'160.0);
    const HorizonRect rect{center.x - 20, center.z - 12, center.x + 20, center.z + 12};
    const std::vector<HorizonCell> cells = sampler.sample(level, rect);
    const HorizonRect image{rect.x0 + counts.x, rect.z0 - 2 * counts.z, rect.x1 + counts.x,
                            rect.z1 - 2 * counts.z};
    TEST_CHECK(sampler.sample(level, image) == cells);

    std::vector<HorizonCell> parts(cells.size());
    const HorizonRect top{rect.x0, rect.z0, rect.x1, rect.z0 + 5};
    const HorizonRect bottom{rect.x0, rect.z0 + 5, rect.x1, rect.z1};
    sampler.fill(level, top, std::span(parts).first(static_cast<std::size_t>(top.count())));
    sampler.fill(level, bottom, std::span(parts).subspan(static_cast<std::size_t>(top.count())));
    TEST_CHECK(parts == cells);
}

// Central differences of the ground at the pitch of the level.
void testGradients() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    const terrain::Ecology ecology(kSeed);
    const HorizonParams params;
    const HorizonSampler sampler(field, ecology, params);
    for (const int level : {0, 4, 9}) {
        const int pitch = terrain::levelPitch(params, level);
        const HorizonIndex c = terrain::canonicalCenter(params, level, -1'441'792.0, -3'932'160.0);
        const HorizonRect rect{c.x - 3, c.z - 3, c.x + 3, c.z + 3};
        const std::vector<HorizonCell> cells = sampler.sample(level, rect);
        std::size_t n = 0;
        bool sloped = false;
        for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
            for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
                const auto h = [&](std::int64_t a, std::int64_t b) {
                    return static_cast<double>(field.heightAt(terrain::samplePosition(a, pitch),
                                                              terrain::samplePosition(b, pitch)));
                };
                const HorizonCell& cell = cells[n++];
                const double run = 2.0 * pitch;
                TEST_CHECK(cell.gradX == static_cast<float>((h(i + 1, k) - h(i - 1, k)) / run));
                TEST_CHECK(cell.gradZ == static_cast<float>((h(i, k + 1) - h(i, k - 1)) / run));
                sloped = sloped || cell.gradX != 0.0f;
            }
        }
        TEST_CHECK(sloped);
    }
}

// A summary of edits raising a square of the world by 50 m and leaving rock on it.
void testEditSummaries() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    const terrain::Ecology ecology(kSeed);
    const HorizonParams params;
    constexpr double kX = -1'441'792.0;
    constexpr double kZ = -3'932'160.0;
    int calls = 0;
    bool footprintsValid = true;
    const terrain::EditSummarySource source = [&](const terrain::CellFootprint& footprint) {
        ++calls; // one thread here
        footprintsValid = footprintsValid && footprint.level == 3 && footprint.halfSide == 32.0 &&
                          std::abs(footprint.centerX) <= params.world.x / 2.0;
        if (std::abs(footprint.centerX - kX) <= 500.0 &&
            std::abs(footprint.centerZ - kZ) <= 500.0) {
            return std::optional<terrain::EditSummary>{{50.0f, terrain::SurfaceMaterial::Rock}};
        }
        return std::optional<terrain::EditSummary>{};
    };
    const HorizonSampler plain(field, ecology, params);
    const HorizonSampler edited(field, ecology, params, source);
    TEST_CHECK(edited.hasEdits() && !plain.hasEdits());
    const HorizonIndex c = terrain::canonicalCenter(params, 3, kX, kZ);
    const HorizonRect rect{c.x - 12, c.z - 12, c.x + 12, c.z + 12};
    const std::vector<HorizonCell> before = plain.sample(3, rect);
    const std::vector<HorizonCell> after = edited.sample(3, rect);
    TEST_CHECK(calls == (rect.width() + 2) * (rect.height() + 2)); // the margin too
    TEST_CHECK(footprintsValid);
    int raised = 0;
    int borderNormals = 0;
    std::size_t n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
            const double x = terrain::samplePosition(i, 64);
            const double z = terrain::samplePosition(k, 64);
            const bool inside = std::abs(x - kX) <= 500.0 && std::abs(z - kZ) <= 500.0;
            if (inside) {
                ++raised;
                TEST_CHECK(after[n].height == before[n].height + 50.0f);
                TEST_CHECK(after[n].material == terrain::SurfaceMaterial::Rock);
                TEST_CHECK((after[n].flags & terrain::kHorizonEdited) != 0);
            } else {
                TEST_CHECK(after[n].height == before[n].height);
                TEST_CHECK(after[n].material == before[n].material);
                TEST_CHECK(after[n].flags == before[n].flags);
                borderNormals += after[n].gradX != before[n].gradX ? 1 : 0;
            }
        }
    }
    TEST_CHECK(raised == 15 * 15); // samples within 500 m of the center, 64 m apart
    TEST_CHECK(borderNormals > 0); // the edited heights shape the normals next to them

    HorizonCell cell;
    cell.height = 10.0f;
    terrain::applyEditSummary({-4.0f, std::nullopt}, cell);
    TEST_CHECK(cell.height == 6.0f && cell.material == terrain::SurfaceMaterial::Water);
    TEST_CHECK(cell.flags == terrain::kHorizonEdited);
}

void testInvalidArguments() {
    const terrain::Heightfield field(kSeed, test_relief::legacyRelief());
    const terrain::Ecology ecology(kSeed);
    const HorizonParams params;
    const auto throws = [](auto&& action) {
        try {
            action();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    HorizonParams other = params;
    other.world = {params.world.x, params.world.z + 65'536.0};
    TEST_CHECK(throws([&] { HorizonSampler sampler(field, ecology, other); }));
    other = params;
    other.levels = 0;
    TEST_CHECK(throws([&] { HorizonSampler sampler(field, ecology, other); }));
    const HorizonSampler sampler(field, ecology, params);
    std::vector<HorizonCell> cells(3);
    TEST_CHECK(throws([&] { sampler.fill(10, {0, 0, 3, 1}, cells); }));
    TEST_CHECK(throws([&] { sampler.fill(-1, {0, 0, 3, 1}, cells); }));
    TEST_CHECK(throws([&] { sampler.fill(0, {0, 0, 2, 2}, cells); }));
    TEST_CHECK(!throws([&] { sampler.fill(0, {0, 0, 3, 1}, cells); }));
    TEST_CHECK(sampler.sample(0, {}).empty());
}

} // namespace

int main() {
    testMaterialOracle();
    testDefaultWorldCoast();
    testPeriodicAndSplit();
    testGradients();
    testEditSummaries();
    testInvalidArguments();
    return test::result();
}
