// The dev map of the generated world (map_source.hpp): the same bytes on 1 and 4 threads, the
// tiles of the pyramid equal to the boxes they cover (across the seams of the wrapped world too),
// the samples of the fine relief those of the heightfield bit for bit, the source chosen by the
// spacing, and the rivers, lakes and falls of the small world with its water.
#include "map_world_support.hpp"
#include "test_assert.hpp"

#include <bit>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace terrain;

namespace {

const std::vector<std::string> kAll = defaultMapLayers().names();

// The bytes of image b at columns [x0, x0 + width of a) of a.
bool sameColumns(const MapImage& a, const MapImage& whole, int x0) {
    bool same = a.height == whole.height;
    for (int j = 0; same && j < a.height; ++j) {
        same = std::memcmp(&a.rgba[4u * static_cast<std::size_t>(j * a.width)],
                           &whole.rgba[4u * static_cast<std::size_t>(j * whole.width + x0)],
                           4u * static_cast<std::size_t>(a.width)) == 0;
    }
    return same;
}

// Two tiles side by side and the box over both, rendered on another number of threads.
bool tilesMatchBox(const MapSampler& sampler, const MapTileKey& left, const WorldSize& world) {
    const MapTileKey right{left.level, left.tx + 1, left.tz};
    const MapBox box = mapTileBox(left, world);
    const MapFrame both({box.x, box.z, 2.0 * box.width, box.height}, 2 * kMapTilePixels,
                        kMapTilePixels, world);
    const MapLayers layers = defaultMapLayers();
    const MapImage whole = renderMap(sampler, both, layers, kAll, {4});
    const MapImage a = renderMap(sampler, mapTileFrame(left, world), layers, kAll, {1});
    const MapImage b = renderMap(sampler, mapTileFrame(right, world), layers, kAll, {1});
    return sameColumns(a, whole, 0) && sameColumns(b, whole, kMapTilePixels);
}

void testDryTiles() {
    const map_world::DryWorld& w = map_world::dry();
    // Near the K2 (the mountains), at 4 m and at 256 m per pixel.
    TEST_CHECK(tilesMatchBox(w.source, mapTileAt(2, 416857.0, -11405966.0), {}));
    TEST_CHECK(tilesMatchBox(w.source, mapTileAt(8, 416857.0, -11405966.0), {}));
    // The last tile of a row of level 3 and the first one: across the east edge of the world.
    const MapTileKey last{3, mapTileColumns(3) - 1, mapTileAt(3, 0.0, -3000.0).tz};
    TEST_CHECK(tilesMatchBox(w.source, last, {}));
}

// The samples of the fine relief: Heightfield::sampleAt at the pixel centers, bit for bit.
void testFineSamples() {
    const map_world::DryWorld& w = map_world::dry();
    const MapFrame f = map_world::frameAround(-3985.0, -165.0, 4.0, 24);
    const std::vector<MapSample> s = map_world::samplesOf(w.source, f, 3);
    bool same = true;
    for (int j = -1; j <= f.height(); ++j) {
        for (int i = -1; i <= f.width(); ++i) {
            const HeightSample h = w.field.sampleAt(f.pixelX(i), f.pixelZ(j));
            const MapSample& m = s[mapGridIndex(f, i, j)];
            same = same && std::bit_cast<std::uint32_t>(m.ground) ==
                               std::bit_cast<std::uint32_t>(h.height) &&
                   m.water == h.water && m.source == MapSourceKind::FineRelief;
        }
    }
    TEST_CHECK(same);
}

void testKinds() {
    const map_world::DryWorld& dry = map_world::dry();
    TEST_CHECK(dry.source.kindFor(1.0) == MapSourceKind::FineRelief);
    TEST_CHECK(dry.source.kindFor(64.0) == MapSourceKind::FineRelief);
    TEST_CHECK(dry.source.kindFor(128.0) == MapSourceKind::Relief);
    TEST_CHECK(dry.source.kindFor(32768.0) == MapSourceKind::Relief); // no planet level
    TEST_CHECK(!dry.source.hasPlanet());
    const map_world::WetWorld wet;
    TEST_CHECK(wet.source.hasPlanet());
    TEST_CHECK(wet.source.kindFor(64.0) == MapSourceKind::FineRelief);
    TEST_CHECK(wet.source.kindFor(1024.0) == MapSourceKind::Relief);
    TEST_CHECK(wet.source.kindFor(2048.0) == MapSourceKind::PlanetCells);
}

// Around the lip of a fall of the small world at 2 m per pixel: its river, its mark, the same
// bytes on 1 and 4 threads; and the whole small world from its planet level.
void testWater() {
    const map_world::FallPlace fall = map_world::firstFall();
    TEST_CHECK(fall.drop >= 20.0f);
    const map_world::WetWorld w;
    const MapFrame f = map_world::frameAround(fall.x, fall.z, 2.0, 128, golden::kSmallPlanet);
    MapSampleStats stats;
    const std::vector<MapSample> s = map_world::samplesOf(w.source, f, 4, &stats);
    const MapSample& lip = s[mapGridIndex(f, static_cast<int>(f.columnAt(fall.x)),
                                          static_cast<int>(f.rowAt(fall.z)))];
    TEST_CHECK(lip.fallMark == kMapFallCenter && lip.fallDrop >= fall.drop);
    // At 2 m per pixel the channel is wide: the river samples themselves, with the discharge of
    // their course; no course is drawn over them.
    std::size_t river = 0;
    std::size_t drawn = 0;
    for (const MapSample& m : s) {
        river += m.water == WaterKind::River && m.discharge > 0.0f ? 1u : 0u;
        drawn += m.riverCover > 0.0f ? 1u : 0u;
    }
    TEST_CHECK(river > 0 && drawn == 0 && stats.hydrologyCalls > 0);
    // At 64 m per pixel the channels are narrower than 3 pixels: drawn over the samples.
    const MapFrame coarse = map_world::frameAround(fall.x, fall.z, 64.0, 64, golden::kSmallPlanet);
    std::size_t covered = 0;
    for (const MapSample& m : map_world::samplesOf(w.source, coarse, 4)) {
        covered += m.riverCover > 0.0f && m.discharge > 0.0f ? 1u : 0u;
    }
    TEST_CHECK(covered > 0);
    const MapLayers layers = defaultMapLayers();
    TEST_CHECK(renderMap(w.source, f, layers, kAll, {1}).rgba ==
               renderMap(w.source, f, layers, kAll, {4}).rgba);
    // The whole small world at 8 km per pixel: the cells of its planet level.
    const WorldSize& small = golden::kSmallPlanet;
    const MapFrame all({-0.5 * small.x, -0.5 * small.z, small.x, small.z}, 256, 128, small);
    const std::vector<MapSample> cells = map_world::samplesOf(w.source, all, 4);
    bool fromCells = true;
    for (int j = 0; j < all.height(); ++j) {
        for (int i = 0; i < all.width(); ++i) {
            const MapSample& m = cells[mapGridIndex(all, i, j)];
            const bool ocean = w.planet->sampleAt(all.pixelX(i), all.pixelZ(j)).ocean;
            fromCells = fromCells && m.source == MapSourceKind::PlanetCells &&
                        (m.water == WaterKind::Ocean) == ocean;
        }
    }
    TEST_CHECK(fromCells);
}

} // namespace

int main() {
    testDryTiles();
    testFineSamples();
    testKinds();
    testWater();
    return test::result();
}
