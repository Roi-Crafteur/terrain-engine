// Terrain surface seen by the ecology: an exact copy of the relief (heightfield, grids, eroded
// region), its water, slopes, steepness and river distances. The sculpted surface is checked
// in ecology_cliffs_test.cpp.
#include "terrain/generation/terrain_surface.hpp"

#include "terrain/generation/climate.hpp"
#include "terrain/generation/height_grid.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using terrain::HeightmapRegion;
using terrain::kSeaLevel;
using terrain::SurfaceSite;
using terrain::TerrainSurface;
using terrain::WaterKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// Coast closest to the origin on a lattice of 1024 m: ocean to the west, land to the east.
// Centers and spacings are powers of two, so sample coordinates are exact.
HeightmapRegion coastRegion(double metersPerPixel, int size) {
    const terrain::Heightfield field(kSeed);
    for (int radius = 0; radius <= 60; ++radius) {
        for (int j = -radius; j <= radius; ++j) {
            for (int i = -radius; i <= radius; ++i) {
                const double x = i * 1024.0;
                const double z = j * 1024.0;
                if (std::max(std::abs(i), std::abs(j)) == radius &&
                    field.heightAt(x, z) < kSeaLevel &&
                    field.heightAt(x + 1024.0, z) >= kSeaLevel) {
                    return {x, z, metersPerPixel, size};
                }
            }
        }
    }
    return {};
}

bool sameSite(const SurfaceSite& a, const SurfaceSite& b) {
    return a.ground == b.ground && a.waterLevel == b.waterLevel && a.water == b.water &&
           a.gradX == b.gradX && a.gradZ == b.gradZ && a.steepness == b.steepness &&
           a.riverDistance == b.riverDistance && a.continentalness == b.continentalness &&
           a.rockAboveAir == b.rockAboveAir && a.cliffWall == b.cliffWall;
}

// The ground is exactly Heightfield::heightAt, the ocean exactly the ground below sea level.
void testHeightfieldSurface() {
    const terrain::Heightfield field(kSeed);
    const HeightmapRegion region = coastRegion(64.0, 64);
    const TerrainSurface surface = terrain::surfaceFromHeightfield(field, region);
    TEST_CHECK(surface.size() == 64 && surface.sites.size() == 64 * 64);
    bool exact = true;
    bool water = true;
    int ocean = 0;
    for (int row = 0; row < surface.size(); ++row) {
        for (int column = 0; column < surface.size(); ++column) {
            const double x = surface.sampleX(column);
            const double z = surface.sampleZ(row);
            const SurfaceSite& site = surface.at(column, row);
            exact = exact && site.ground == field.heightAt(x, z) &&
                    site.continentalness == field.sampleAt(x, z).continentalness;
            const bool under = site.ground < kSeaLevel;
            water = water && (site.water == (under ? WaterKind::Ocean : WaterKind::None)) &&
                    site.waterLevel == (under ? kSeaLevel : site.ground) &&
                    site.riverDistance == terrain::kNoRiverDistance && !site.rockAboveAir &&
                    !site.cliffWall;
            ocean += under ? 1 : 0;
        }
    }
    TEST_CHECK(exact);
    TEST_CHECK(water);
    TEST_CHECK(ocean > 0 && ocean < 64 * 64);
    TEST_CHECK(surface.sampleX(0) == region.centerX - 31.5 * 64.0);
}

// Inside, the pointwise surface of the heightfield is exactly the surface of its sampled grid.
void testHeightfieldMatchesGrid() {
    const terrain::Heightfield field(kSeed);
    const HeightmapRegion region = coastRegion(64.0, 48);
    const TerrainSurface pointwise = terrain::surfaceFromHeightfield(field, region);
    const terrain::HeightGrid grid = terrain::sampleHeightGrid(field, region);
    const TerrainSurface gridded =
        terrain::surfaceFromGrids(field, region, {.ground = grid.heights});
    bool same = true;
    for (int row = 1; row + 1 < region.size; ++row) {
        for (int column = 1; column + 1 < region.size; ++column) {
            same = same && sameSite(pointwise.at(column, row), gridded.at(column, row));
        }
    }
    TEST_CHECK(same);
}

// On a tilted plane the gradient is exact everywhere, borders included, and the steepness is
// the steeper axis inside.
void testPlane() {
    const terrain::Heightfield field(kSeed);
    const HeightmapRegion region{.metersPerPixel = 10.0, .size = 16};
    TerrainSurface probe{region, {}};
    std::vector<float> heights;
    for (int row = 0; row < region.size; ++row) {
        for (int column = 0; column < region.size; ++column) {
            heights.push_back(
                static_cast<float>(300.0 + 0.3 * probe.sampleX(column) - 0.2 * probe.sampleZ(row)));
        }
    }
    const TerrainSurface surface = terrain::surfaceFromGrids(field, region, {.ground = heights});
    bool gradient = true;
    bool steepness = true;
    for (int row = 0; row < region.size; ++row) {
        for (int column = 0; column < region.size; ++column) {
            const SurfaceSite& site = surface.at(column, row);
            gradient = gradient && std::abs(site.gradX - 0.3f) < 1e-4f &&
                       std::abs(site.gradZ + 0.2f) < 1e-4f && site.dry();
            if (row > 0 && column > 0 && row + 1 < region.size && column + 1 < region.size) {
                steepness = steepness && std::abs(site.steepness - 0.3f) < 1e-4f;
            }
        }
    }
    TEST_CHECK(gradient);
    TEST_CHECK(steepness);
    // The plane goes down toward +Z: it faces the sun.
    const SurfaceSite& middle = surface.at(8, 8);
    TEST_CHECK(terrain::exposureOf(middle.gradX, middle.gradZ) > 0.0f);
}

// Water, river distances (chamfer, 8-connected) and masks of synthetic grids.
void testRiversAndMasks() {
    const terrain::Heightfield field(kSeed);
    constexpr int kSize = 21;
    const HeightmapRegion region{.metersPerPixel = 10.0, .size = kSize};
    const auto count = static_cast<std::size_t>(kSize * kSize);
    std::vector<float> heights(count, 50.0f);
    std::vector<std::uint8_t> river(count, 0);
    std::vector<std::uint8_t> air(count, 0);
    std::vector<std::uint8_t> walls(count, 0);
    const auto at = [](int column, int row) {
        return static_cast<std::size_t>(row * kSize + column);
    };
    river[at(10, 10)] = 1;
    heights[at(0, 20)] = -5.0f;
    air[at(3, 4)] = 1;
    walls[at(5, 6)] = 1;
    const TerrainSurface surface = terrain::surfaceFromGrids(
        field, region,
        {.ground = heights, .river = river, .rockAboveAir = air, .cliffWall = walls});
    const SurfaceSite& source = surface.at(10, 10);
    TEST_CHECK(source.water == WaterKind::River && source.waterLevel == 50.0f &&
               source.riverDistance == 0.0f && source.waterDepth() == 0.0f);
    TEST_CHECK(std::abs(surface.at(13, 10).riverDistance - 30.0f) < 1e-3f);
    TEST_CHECK(std::abs(surface.at(10, 0).riverDistance - 100.0f) < 1e-3f);
    TEST_CHECK(std::abs(surface.at(12, 12).riverDistance - 20.0f * std::sqrt(2.0f)) < 1e-3f);
    TEST_CHECK(std::abs(surface.at(0, 0).riverDistance - 100.0f * std::sqrt(2.0f)) < 1e-2f);
    const SurfaceSite& sea = surface.at(0, 20);
    TEST_CHECK(sea.water == WaterKind::Ocean && sea.waterLevel == kSeaLevel &&
               sea.waterDepth() == 5.0f);
    TEST_CHECK(surface.at(3, 4).rockAboveAir && !surface.at(4, 4).rockAboveAir);
    TEST_CHECK(surface.at(5, 6).cliffWall && !surface.at(6, 6).cliffWall);
    TEST_CHECK(surface.at(1, 1).dry() && surface.at(1, 1).steepness == 0.0f);
    // The steepness sees the drop to the ocean sample, the gradient is one-sided there.
    TEST_CHECK(std::abs(surface.at(0, 19).steepness - 5.5f) < 1e-5f);
}

// The surface of an eroded region is its final grid, bit for bit, with its rivers; building it
// changes nothing in the region.
void testErodedSurface() {
    const terrain::ErodedRegion eroded(kSeed, coastRegion(100.0, 96));
    const std::vector<float> before = eroded.grid().heights;
    const TerrainSurface surface = terrain::surfaceFromEroded(eroded);
    TEST_CHECK(eroded.grid().heights == before);
    const auto& rivers = eroded.rivers();
    bool same = true;
    int riverCount = 0;
    int oceanCount = 0;
    for (std::size_t i = 0; i < surface.sites.size(); ++i) {
        const SurfaceSite& site = surface.sites[i];
        const bool river = rivers.isRiver(static_cast<int>(i));
        const WaterKind expected =
            river ? WaterKind::River : (before[i] < kSeaLevel ? WaterKind::Ocean : WaterKind::None);
        same = same && site.ground == before[i] && site.water == expected &&
               (site.riverDistance == 0.0f) == river;
        riverCount += river ? 1 : 0;
        oceanCount += site.water == WaterKind::Ocean ? 1 : 0;
    }
    TEST_CHECK(same);
    TEST_CHECK(riverCount > 0 && oceanCount > 0);
    TEST_CHECK(surface.region.centerX == eroded.region().centerX);
}

} // namespace

int main() {
    testHeightfieldSurface();
    testHeightfieldMatchesGrid();
    testPlane();
    testRiversAndMasks();
    testErodedSurface();
    return test::result();
}
