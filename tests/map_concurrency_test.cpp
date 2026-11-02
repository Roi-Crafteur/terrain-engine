// The dev map read at the same time as the streaming (map_source.hpp): one thread renders tiles of
// the map while another samples the same heightfield (as the workers of the tiles do), over a
// hydrology that computes its courses as they come; the same bytes and the same bits as one after
// the other.
#include "map_world_support.hpp"
#include "test_assert.hpp"

#include <bit>
#include <cstdint>
#include <thread>
#include <vector>

using namespace terrain;

namespace {

struct Work {
    std::vector<MapTileKey> tiles;
    std::vector<std::pair<double, double>> points;
};

// Tiles of 2, 16 and 64 m per pixel around the first fall, and points on a grid over the same
// ground.
Work workAround(const map_world::FallPlace& fall) {
    Work work;
    for (const int level : {1, 4, 6}) {
        const MapTileKey middle = mapTileAt(level, fall.x, fall.z, golden::kSmallPlanet);
        work.tiles.push_back(middle);
        work.tiles.push_back({level, middle.tx + 1, middle.tz});
    }
    for (int j = -40; j < 40; ++j) {
        for (int i = -40; i < 40; ++i) {
            work.points.emplace_back(fall.x + 25.0 * i + 0.5, fall.z + 25.0 * j + 0.25);
        }
    }
    return work;
}

std::vector<std::vector<std::uint8_t>> renderAll(const MapWorldSource& source, const Work& work) {
    const MapLayers layers = defaultMapLayers();
    std::vector<std::vector<std::uint8_t>> images;
    for (const MapTileKey& key : work.tiles) {
        images.push_back(renderMap(source, mapTileFrame(key, golden::kSmallPlanet), layers,
                                   layers.names(), {2})
                             .rgba);
    }
    return images;
}

std::vector<std::uint32_t> sampleAll(const Heightfield& field, const Work& work) {
    std::vector<std::uint32_t> bits;
    for (const auto& [x, z] : work.points) {
        const HeightSample s = field.sampleAt(x, z);
        bits.push_back(std::bit_cast<std::uint32_t>(s.height));
        bits.push_back(std::bit_cast<std::uint32_t>(s.waterLevel));
        bits.push_back(static_cast<std::uint32_t>(s.water));
    }
    return bits;
}

void testTogether() {
    const Work work = workAround(map_world::firstFall());
    // One after the other, on a world of its own.
    std::vector<std::vector<std::uint8_t>> serialImages;
    std::vector<std::uint32_t> serialBits;
    {
        const map_world::WetWorld w;
        serialImages = renderAll(w.source, work);
        serialBits = sampleAll(w.tiles.heightfield(), work);
    }
    // At the same time, on a fresh world: both threads meet the same courses not yet computed.
    const map_world::WetWorld w;
    std::vector<std::vector<std::uint8_t>> images;
    std::vector<std::uint32_t> bits;
    std::thread map([&] { images = renderAll(w.source, work); });
    std::thread streaming([&] { bits = sampleAll(w.tiles.heightfield(), work); });
    map.join();
    streaming.join();
    TEST_CHECK(images == serialImages);
    TEST_CHECK(bits == serialBits);
    TEST_CHECK(w.hydrology->coursesComputed() > 0);
}

} // namespace

int main() {
    testTogether();
    return test::result();
}
