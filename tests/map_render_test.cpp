// The dev map of the generated world (map_source.hpp): the same bytes on 1 and 4 threads, the
// tiles of the pyramid equal to the boxes they cover (across the seams of the wrapped world too),
// the samples of the fine relief those of the heightfield bit for bit, the source chosen by the
// spacing, and the rivers, lakes and falls of the small world with its water.
#include "map_world_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <thread>
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
    // There every channel is narrower than 3 pixels: drawn by its antialiased line alone, no river
    // sample left (they would draw steps along it).
    std::size_t opaque = 0;
    for (const MapSample& m : map_world::samplesOf(w.source, coarse, 4)) {
        opaque += m.water == WaterKind::River ? 1u : 0u;
    }
    TEST_CHECK(opaque == 0);
    // Around the mouths of rivers into the sea at 64 m per pixel: their courses drawn on the land,
    // never over the sea (a course goes on to the middle of the cell of its mouth).
    std::size_t land = 0;
    std::size_t sea = 0;
    std::size_t mouths = 0;
    for (const RiverSegment& segment : w.planet->rivers().segments) {
        if (segment.mouth != RiverMouth::Sea || segment.vertices.front().discharge < 50.0f ||
            ++mouths > 4) {
            continue;
        }
        const RiverVertex& end = segment.vertices.back();
        const MapFrame f64 = map_world::frameAround(end.x, end.z, 64.0, 128, golden::kSmallPlanet);
        for (const MapSample& m : map_world::samplesOf(w.source, f64, 4)) {
            land += m.riverCover > 0.0f && m.water == WaterKind::None ? 1u : 0u;
            sea += m.riverCover > 0.0f && m.water == WaterKind::Ocean ? 1u : 0u;
        }
    }
    TEST_CHECK(mouths > 0 && land > 0 && sea == 0);
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

// The ground at or above the surface of a sea or a lake is land on the map, as Unreal shows it
// (the banks a valley carries out into the sea); rivers stay rivers.
void testEmergedGround() {
    TEST_CHECK(mapWaterOf(WaterKind::Ocean, 0.69f, 0.0f) == WaterKind::None);
    TEST_CHECK(mapWaterOf(WaterKind::Ocean, 0.0f, 0.0f) == WaterKind::None);
    TEST_CHECK(mapWaterOf(WaterKind::Ocean, -0.93f, 0.0f) == WaterKind::Ocean);
    TEST_CHECK(mapWaterOf(WaterKind::Lake, 201.0f, 200.75f) == WaterKind::None);
    TEST_CHECK(mapWaterOf(WaterKind::SaltLake, 10.0f, 12.0f) == WaterKind::SaltLake);
    TEST_CHECK(mapWaterOf(WaterKind::River, 5.0f, 4.0f) == WaterKind::River);
    TEST_CHECK(mapWaterOf(WaterKind::None, 5.0f, 5.0f) == WaterKind::None);
}

// The whole small world at 32 km per pixel, 4 x 4 cells of 8 km in a pixel: each pixel takes the
// water most of its 3 x 3 points have (the oracle: PlanetLevel::sampleAt at the same points), and
// fewer water pixels stand alone among dry ones than with the point at their center only.
void testPlanetPixels() {
    const map_world::WetWorld w;
    const WorldSize& small = golden::kSmallPlanet;
    const MapFrame f({-0.5 * small.x, -0.5 * small.z, small.x, small.z}, 64, 32, small);
    const std::vector<MapSample> s = map_world::samplesOf(w.source, f, 4);
    const auto kindAt = [&](double x, double z) {
        const PlanetSample p = w.planet->sampleAt(x, z);
        if (p.lake >= 0 && !p.ocean) {
            return w.planet->lakes()[static_cast<std::size_t>(p.lake)].playa ? 3 : 2;
        }
        return p.ocean ? 1 : 0;
    };
    bool majority = true;
    std::size_t aloneMap = 0;
    std::size_t aloneCenter = 0;
    for (int j = 0; j < f.height(); ++j) {
        for (int i = 0; i < f.width(); ++i) {
            std::array<int, 4> votes{}; // none, sea, lake, salt lake: the order of WaterKind
            for (int b = 0; b < kMapPlanetSubPoints; ++b) {
                for (int a = 0; a < kMapPlanetSubPoints; ++a) {
                    ++votes[static_cast<std::size_t>(
                        kindAt(f.pixelX(i) + ((a + 0.5) / 3.0 - 0.5) * f.stepX(),
                               f.pixelZ(j) + ((b + 0.5) / 3.0 - 0.5) * f.stepZ()))];
                }
            }
            const int expected = static_cast<int>(
                std::distance(votes.begin(), std::max_element(votes.begin(), votes.end())));
            const WaterKind got = s[mapGridIndex(f, i, j)].water;
            // none 0, sea 1, lake 2, salt lake 3 (WaterKind less its river)
            const int kind =
                got == WaterKind::None || got == WaterKind::Ocean ? static_cast<int>(got)
                                                                  : static_cast<int>(got) - 1;
            majority = majority && kind == expected;
            const auto wetAt = [&](int di, int dj, bool center) {
                const MapSample& m = s[mapGridIndex(f, i + di, j + dj)];
                return center ? kindAt(f.pixelX(i + di), f.pixelZ(j + dj)) != 0
                              : m.water != WaterKind::None;
            };
            for (const bool center : {false, true}) {
                const bool alone = wetAt(0, 0, center) && !wetAt(1, 0, center) &&
                                   !wetAt(-1, 0, center) && !wetAt(0, 1, center) &&
                                   !wetAt(0, -1, center);
                (center ? aloneCenter : aloneMap) += alone ? 1u : 0u;
            }
        }
    }
    std::printf("small world at 32 km/px: %zu water pixels alone (%zu with the centers only)\n",
                aloneMap, aloneCenter);
    TEST_CHECK(majority);
    TEST_CHECK(aloneMap < aloneCenter);
}

// The flag raised before the render, or during it from another thread: MapCancelled, soon.
void testCancel() {
    const map_world::DryWorld& w = map_world::dry();
    const MapFrame f = map_world::frameAround(416857.0, -11405966.0, 4.0, 256);
    std::atomic<bool> raised{true};
    bool thrown = false;
    try {
        (void)renderMap(w.source, f, defaultMapLayers(), kAll, {1, kMapMaxSamples, &raised});
    } catch (const MapCancelled&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
    std::atomic<bool> later{false};
    std::atomic<bool> cancelled{false};
    std::chrono::steady_clock::time_point raisedAt;
    std::thread render([&] {
        try {
            (void)renderMap(w.source, map_world::frameAround(416857.0, -11405966.0, 1.0, 1024),
                            defaultMapLayers(), kAll, {1, kMapMaxSamples, &later});
        } catch (const MapCancelled&) {
            cancelled = true;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    raisedAt = std::chrono::steady_clock::now();
    later = true;
    render.join();
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raisedAt).count();
    std::printf("render of 1024 x 1024 pixels stopped %.1f ms after its flag\n", ms);
    TEST_CHECK(cancelled.load());
}

} // namespace

int main() {
    testDryTiles();
    testFineSamples();
    testKinds();
    testWater();
    testEmergedGround();
    testPlanetPixels();
    testCancel();
    return test::result();
}
