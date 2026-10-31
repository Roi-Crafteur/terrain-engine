// The pixels of the dev map on the wrapped world (map_frame.hpp): pixel <-> world exactly, the
// tiles of the pyramid and their seams.
#include "terrain/map/map_frame.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace terrain;

namespace {

const WorldSize kWorld{};
const WorldSize kSmall{65536.0 * 4, 65536.0 * 2};

// Every pixel of a frame comes back to its column and row from its center.
bool roundTrips(const MapFrame& f) {
    for (int i = 0; i < f.width(); ++i) {
        if (f.columnAt(f.pixelX(i)) != i) {
            return false;
        }
    }
    for (int j = 0; j < f.height(); ++j) {
        if (f.rowAt(f.pixelZ(j)) != j) {
            return false;
        }
    }
    return true;
}

void testLevels() {
    TEST_CHECK(mapLevelMeters(0) == 1.0);
    TEST_CHECK(mapLevelMeters(13) == 8192.0);
    TEST_CHECK(mapLevelMeters(kMapMaxLevel) == 32768.0);
    TEST_CHECK(mapLevelFor(0.25) == 0);
    TEST_CHECK(mapLevelFor(1.0) == 0);
    TEST_CHECK(mapLevelFor(1.5) == 1);
    TEST_CHECK(mapLevelFor(4.0) == 2);
    TEST_CHECK(mapLevelFor(31266.0) == 15);
    TEST_CHECK(mapLevelFor(1.0e9) == kMapMaxLevel);
    // One pixel of level 13 is one cell of the planet level: 7,328 x 4,272.
    TEST_CHECK(mapWorldColumns(13) == 7328);
    TEST_CHECK(mapWorldRows(13) == 4272);
    TEST_CHECK(mapWorldColumns(15) == 1832);
    TEST_CHECK(mapWorldRows(15) == 1068);
    TEST_CHECK(mapTileColumns(15) == 8);
    TEST_CHECK(mapTileRows(15) == 5);
    TEST_CHECK(mapWorldColumns(0) == 60'030'976);
    bool threw = false;
    try {
        (void)mapLevelMeters(kMapMaxLevel + 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

// Every pixel of tiles of every level, on the edges of the world and inside, comes back to its
// column and row; a tile is 256 pixels of 2^L meters, from the west and north edges.
void testTiles() {
    for (int level = 0; level <= kMapMaxLevel; ++level) {
        const std::int64_t lastX = mapTileColumns(level) - 1;
        const std::int64_t lastZ = mapTileRows(level) - 1;
        for (const MapTileKey key : {MapTileKey{level, 0, 0}, MapTileKey{level, lastX, lastZ},
                                     MapTileKey{level, lastX / 2, lastZ / 3}}) {
            const MapFrame f = mapTileFrame(key);
            TEST_CHECK(roundTrips(f));
            TEST_CHECK(f.stepX() == mapLevelMeters(level) && f.stepZ() == mapLevelMeters(level));
            TEST_CHECK(mapTileAt(level, f.pixelX(0), f.pixelZ(0)) == key);
            // The last pixel of the tile inside the world (a last tile ends at the edge).
            const auto inside = [](std::int64_t count, std::int64_t t) {
                return std::min<std::int64_t>(kMapTilePixels, count - t * kMapTilePixels) - 1;
            };
            const std::int64_t ei = inside(mapWorldColumns(level), key.tx);
            const std::int64_t ej = inside(mapWorldRows(level), key.tz);
            TEST_CHECK(mapTileAt(level, f.pixelX(ei), f.pixelZ(ej)) == key);
        }
    }
    const MapBox first = mapTileBox({0, 0, 0});
    TEST_CHECK(first.x == -0.5 * kWorld.x && first.z == -0.5 * kWorld.z);
    TEST_CHECK(first.width == 256.0 && first.height == 256.0);
    TEST_CHECK((wrapTile({3, -1, -1}) ==
                MapTileKey{3, mapTileColumns(3) - 1, mapTileRows(3) - 1}));
    TEST_CHECK((wrapTile({3, mapTileColumns(3), 2 * mapTileRows(3) + 1}) == MapTileKey{3, 0, 1}));
}

// The last tile of a row reaches past the east edge of the world: its pixels beyond it are the
// first pixels of the row again, the same centers bit for bit; along z too.
void testSeams() {
    for (const int level : {0, 7, 13, 15}) {
        const std::int64_t lastX = mapTileColumns(level) - 1;
        const std::int64_t inLast = mapWorldColumns(level) - lastX * kMapTilePixels;
        const MapFrame last = mapTileFrame({level, lastX, 0});
        const MapFrame first = mapTileFrame({level, 0, 0});
        bool same = true;
        for (std::int64_t c = inLast; c < kMapTilePixels; ++c) {
            same = same && last.pixelX(c) == first.pixelX(c - inLast);
        }
        TEST_CHECK(same);
        TEST_CHECK(last.pixelX(inLast - 1) > last.pixelX(inLast)); // wrapped: the far west
        const std::int64_t lastZ = mapTileRows(level) - 1;
        const std::int64_t inLastZ = mapWorldRows(level) - lastZ * kMapTilePixels;
        const MapFrame bottom = mapTileFrame({level, 0, lastZ});
        bool sameZ = true;
        for (std::int64_t r = inLastZ; r < kMapTilePixels; ++r) {
            sameZ = sameZ && bottom.pixelZ(r) == first.pixelZ(r - inLastZ);
        }
        TEST_CHECK(sameZ);
    }
}

// A box across the seam, and the same box whole worlds away: the same pixel centers, every
// pixel coming back to its place.
void testBoxAcrossSeam() {
    const MapBox across{0.5 * kWorld.x - 300.0, 0.5 * kWorld.z - 200.0, 640.0, 512.0};
    const MapFrame a(across, 320, 256);
    MapBox shifted = across;
    shifted.x -= 3.0 * kWorld.x;
    shifted.z += 2.0 * kWorld.z;
    const MapFrame b(shifted, 320, 256);
    bool same = true;
    for (int i = -1; i <= 320; ++i) {
        same = same && a.pixelX(i) == b.pixelX(i);
    }
    for (int j = -1; j <= 256; ++j) {
        same = same && a.pixelZ(j) == b.pixelZ(j);
    }
    TEST_CHECK(same);
    TEST_CHECK(roundTrips(a) && roundTrips(b));
    // Column 150 is the first one past the edge: the west end of the canonical range.
    TEST_CHECK(a.pixelX(149) == 0.5 * kWorld.x - 1.0);
    TEST_CHECK(a.pixelX(150) == -0.5 * kWorld.x + 1.0);
    TEST_CHECK(a.columnAt(-0.5 * kWorld.x + 1.0) == 150);
    // A point outside the box: its column lies outside [0, width), on its side.
    TEST_CHECK(a.columnAt(0.5 * kWorld.x - 400.0) < 0);
    TEST_CHECK(a.columnAt(-0.5 * kWorld.x + 500.0) >= 320);
}

// Boxes of any size and any number of pixels, also of awkward spacings and on a small world.
void testAnyBox() {
    std::mt19937_64 random(35);
    std::uniform_real_distribution<double> where(-4.0e7, 4.0e7);
    std::uniform_real_distribution<double> size(1.0, 2.0e6);
    std::uniform_int_distribution<int> pixels(1, 700);
    bool all = true;
    for (int n = 0; n < 200; ++n) {
        const WorldSize& world = n % 4 == 3 ? kSmall : kWorld;
        const double w = n % 4 == 3 ? 0.5 * world.x * (size(random) / 2.0e6) : size(random);
        const MapFrame f({where(random), where(random), w, w * 0.6}, pixels(random),
                         pixels(random), world);
        all = all && roundTrips(f);
    }
    TEST_CHECK(all);
}

void testInvalid() {
    int refused = 0;
    const auto attempt = [&](const MapBox& box, int w, int h) {
        try {
            (void)MapFrame(box, w, h);
        } catch (const std::invalid_argument&) {
            ++refused;
        }
    };
    attempt({0, 0, 0, 10}, 10, 10);
    attempt({0, 0, 10, -1}, 10, 10);
    attempt({0, 0, 10, 10}, 0, 10);
    attempt({0, 0, 10, 10}, 10, kMapMaxPixels + 1);
    attempt({std::numeric_limits<double>::quiet_NaN(), 0, 10, 10}, 10, 10);
    TEST_CHECK(refused == 5);
}

} // namespace

int main() {
    testLevels();
    testTiles();
    testSeams();
    testBoxAcrossSeam();
    testAnyBox();
    testInvalid();
    return test::result();
}
