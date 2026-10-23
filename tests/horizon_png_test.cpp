// Control images of the far horizon: the points read from the finest level holding them, the
// level and map grids, the silhouette of a flat sea and of a lone mountain against the curvature
// of the planet (exact formulas), the panorama (sky above, ground below, how far the ground
// shows) and the files written.
#include "terrain/io/heightmap_png.hpp"
#include "terrain/io/horizon_panorama.hpp"
#include "terrain/io/horizon_png.hpp"

#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using horizon_test::kX;
using horizon_test::kZ;

// The mountains around (kX, kZ) of the legacy relief.
struct Fixture {
    horizon_test::World world{test_relief::legacyRelief()};
    terrain::HorizonParams params = horizon_test::smallParams();
    terrain::HorizonSampler sampler{world.field, world.ecology, params};
    terrain::HorizonClipmap clipmap{params};

    Fixture() { horizon_test::settle(clipmap, sampler, kX, kZ); }
};

void testPoints(const Fixture& f) {
    const std::optional<terrain::HorizonPoint> here = terrain::horizonPointAt(f.clipmap, kX, kZ);
    TEST_CHECK(here && here->level == 0 && here->nearest != nullptr);
    TEST_CHECK(here && here->height == *f.clipmap.level(0).heightAt(kX, kZ));
    const std::optional<terrain::HorizonPoint> far =
        terrain::horizonPointAt(f.clipmap, kX + 60'000.0, kZ);
    TEST_CHECK(far && far->level == 8); // 60 km: beyond level 7 (+-33 km with K = 64)
    TEST_CHECK(!terrain::horizonPointAt(f.clipmap, kX + 400'000.0, kZ));
    // The nearest sample of a sample position is that sample.
    const terrain::HorizonLevel& level = f.clipmap.level(3);
    const double x = level.originX(kX) + 5.0 * level.pitch();
    const double z = level.originZ(kZ) + 7.0 * level.pitch();
    const std::optional<terrain::HorizonPoint> sample = terrain::horizonPointAt(f.clipmap, x, z, 3);
    TEST_CHECK(sample && sample->nearest == &level.atWindow(5, 7));
}

void testGrids(const Fixture& f) {
    const terrain::HorizonLevel& level = f.clipmap.level(2);
    const terrain::HeightGrid grid = terrain::levelHeightGrid(level);
    TEST_CHECK(grid.size == level.side() && grid.metersPerPixel == level.pitch());
    TEST_CHECK(grid.at(4, 9) == level.atWindow(4, 9).height);
    // A map of 64 px of 4 m around the observer: level 0 everywhere, bilinear.
    const terrain::HeightGrid map = terrain::horizonMapHeights(f.clipmap, kX, kZ, 64, 4.0);
    const double px = kX + (10 + 0.5 - 32.0) * 4.0;
    const double pz = kZ + (20 + 0.5 - 32.0) * 4.0;
    TEST_CHECK(map.at(10, 20) == *f.clipmap.level(0).heightAt(px, pz));
    const terrain::Image materials = terrain::renderHorizonMaterials(f.clipmap, kX, kZ, 16, 50.0);
    TEST_CHECK(materials.width == 16 && materials.pixels.size() == 16 * 16 * 3);
}

// A flat sea seen from 100 m: the horizon dips by sqrt(2 h / R) at sqrt(2 R h).
void testSilhouetteOfTheSea() {
    terrain::PanoramaParams params;
    params.eyeHeight = 100.0;
    params.width = 36;
    const terrain::GroundHeight sea = [](double, double) { return std::optional<float>(-50.0f); };
    const terrain::Silhouette silhouette =
        terrain::traceSilhouette(sea, params, 0, params.width, 200'000.0);
    const double dip = -std::sqrt(2.0 * 100.0 / params.planetRadius);
    const double distance = std::sqrt(2.0 * params.planetRadius * 100.0);
    for (int c = 0; c < params.width; ++c) {
        TEST_CHECK(std::abs(silhouette.tangent[static_cast<std::size_t>(c)] - dip) < 1e-6);
        TEST_CHECK(std::abs(silhouette.distance[static_cast<std::size_t>(c)] - distance) <
                   0.01 * distance);
    }
}

// A cone of 3000 m, 100 km east of an eye at 1000 m: its top is seen at
// (3000 - 1000 - d^2 / 2R) / d; elsewhere the sea horizon.
void testSilhouetteOfAMountain() {
    terrain::PanoramaParams params;
    params.eyeHeight = 1000.0;
    params.width = 3600;
    const terrain::GroundHeight ground = [](double x, double z) {
        const double r = std::sqrt((x - 100'000.0) * (x - 100'000.0) + z * z);
        return std::optional<float>(static_cast<float>(3000.0 - 0.3 * r));
    };
    const terrain::Silhouette silhouette =
        terrain::traceSilhouette(ground, params, 899, 2, 300'000.0); // facing east
    const double d = 100'000.0;
    const double top = (3000.0 - 1000.0 - d * d / (2.0 * params.planetRadius)) / d;
    for (const double tangent : silhouette.tangent) {
        TEST_CHECK(std::abs(tangent - top) < 0.002);
    }
}

void testPanorama(const Fixture& f) {
    terrain::PanoramaParams params;
    params.eyeX = kX;
    params.eyeZ = kZ;
    params.eyeHeight = *f.clipmap.level(0).heightAt(kX, kZ) + 2.0;
    params.width = 360;
    params.height = 120;
    const terrain::Panorama panorama = terrain::renderHorizonPanorama(f.clipmap, params);
    TEST_CHECK(panorama.image.width == 360 && panorama.image.height == 120);
    TEST_CHECK(panorama.image.pixels.size() == 360 * 120 * 3);
    TEST_CHECK(panorama.image.pixels[0] == 70 && panorama.image.pixels[2] == 200); // zenith
    // The corners of the coarsest window with K = 64 (the crests raise ground there that may
    // form the silhouette along a diagonal, beyond its sides).
    const double reach = std::sqrt(2.0) * (32.0 * 4096.0 + 4096.0);
    int groundColumns = 0;
    for (std::size_t c = 0; c < panorama.farthest.size(); ++c) {
        TEST_CHECK(panorama.farthest[c] <= reach);
        TEST_CHECK(panorama.silhouette.distance[c] <= reach);
        groundColumns += panorama.farthest[c] > 0.0 ? 1 : 0;
    }
    TEST_CHECK(groundColumns == params.width); // the ground under the eye shows everywhere
    const auto throws = [&](terrain::PanoramaParams bad) {
        try {
            static_cast<void>(terrain::renderHorizonPanorama(f.clipmap, bad));
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    terrain::PanoramaParams bad = params;
    bad.width = 1;
    TEST_CHECK(throws(bad));
    bad = params;
    bad.exaggeration = 0.0;
    TEST_CHECK(throws(bad));
    bad = params;
    bad.eyeX = kX + 400'000.0;
    TEST_CHECK(throws(bad));
}

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void testExport(const Fixture& f) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_horizon_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportHorizonMaps(f.clipmap, kX, kZ, directory));
    for (int n = 0; n < f.params.levels; ++n) {
        TEST_CHECK(std::filesystem::exists(directory / ("level_" + std::to_string(n) + ".png")));
    }
    const std::vector<std::uint8_t> expected = terrain::encodePng(
        terrain::renderHeightmapPreview(terrain::levelHeightGrid(f.clipmap.level(0))));
    const std::vector<char> written = readFile(directory / "level_0.png");
    TEST_CHECK(
        written.size() == expected.size() &&
        std::equal(written.begin(), written.end(), expected.begin(),
                   [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; }));
    TEST_CHECK(std::filesystem::exists(directory / "map_relief.png"));
    TEST_CHECK(std::filesystem::exists(directory / "map_materials.png"));
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    const Fixture fixture;
    testPoints(fixture);
    testGrids(fixture);
    testSilhouetteOfTheSea();
    testSilhouetteOfAMountain();
    testPanorama(fixture);
    testExport(fixture);
    return test::result();
}
