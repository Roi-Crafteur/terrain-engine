// The tiles of the dev map on threads of their own (Bridge/map_tile_cache.hpp): the bytes of
// renderMap, kept and reused (moving a view renders only its new tiles), the least used dropped
// beyond the capacity, the stale requests dropped, the layers changed, the probe of the cursor,
// and a clean stop while tiles render.
#include "map_tile_cache.hpp"

#include "map_test_support.hpp"
#include "map_view.hpp"
#include "test_assert.hpp"

#include "terrain/map/map_style.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace terrain;
using namespace terrain::unreal;

namespace {

// Tiles of the synthetic world (no generation), counted.
struct Counted {
    std::shared_ptr<std::atomic<int>> calls = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> delayMs = std::make_shared<std::atomic<int>>(0);

    MapTileCache::Render render() const {
        auto c = calls;
        auto d = delayMs;
        return [c, d](const MapTileKey& key, const std::vector<std::string>& layers) {
            ++*c;
            std::this_thread::sleep_for(std::chrono::milliseconds(d->load()));
            static const map_test::SyntheticSampler sampler;
            static const MapLayers registry = defaultMapLayers();
            return renderMap(sampler, mapTileFrame(key), registry, layers);
        };
    }
};

MapTileCache::Probe noProbe() {
    return [](double x, double z) {
        MapProbe p;
        p.x = x;
        p.z = z;
        p.ground = map_test::groundAt(x, z);
        return p;
    };
}

std::vector<MapTileKey> keysOf(const std::vector<MapTileDraw>& draws) {
    std::vector<MapTileKey> keys;
    for (const MapTileDraw& d : draws) {
        keys.push_back(d.key);
    }
    return keys;
}

void testRenderAndReuse() {
    const Counted counted;
    MapTileCache cache(counted.render(), noProbe(), defaultMapLayerNames(), {64, 2});
    MapView view;
    view.metersPerPixel = 4.0;
    view.width = 1024.0;
    view.height = 512.0;
    const std::vector<MapTileKey> first = keysOf(tilesInView(view));
    cache.want(first);
    cache.waitIdle();
    TEST_CHECK(cache.kept() == first.size() && counted.calls->load() == static_cast<int>(first.size()));
    TEST_CHECK(cache.takeReady().size() == first.size() && cache.takeReady().empty());
    // The bytes of renderMap.
    const map_test::SyntheticSampler sampler;
    const std::shared_ptr<const MapTile> tile = cache.find(first.front());
    TEST_CHECK(tile != nullptr && tile->revision == cache.revision());
    TEST_CHECK(tile->image.rgba == renderMap(sampler, mapTileFrame(first.front()), defaultMapLayers(),
                                             defaultMapLayerNames())
                                       .rgba);
    // Dragged by one tile: only the new column of tiles is rendered.
    const MapView moved = panBy(view, -256.0, 0.0);
    const std::vector<MapTileKey> second = keysOf(tilesInView(moved));
    std::size_t fresh = 0;
    for (const MapTileKey& k : second) {
        fresh += cache.find(k) == nullptr ? 1u : 0u;
    }
    cache.want(second);
    cache.waitIdle();
    TEST_CHECK(fresh > 0 && fresh < second.size());
    TEST_CHECK(counted.calls->load() == static_cast<int>(first.size() + fresh));
    TEST_CHECK(cache.lastError().empty());
}

// Beyond the capacity the tiles used the longest ago go; a new layer drops every tile.
void testCapacityAndLayers() {
    const Counted counted;
    MapTileCache cache(counted.render(), noProbe(), {"relief"}, {4, 1});
    const std::vector<MapTileKey> keys = {{3, 0, 0}, {3, 1, 0}, {3, 2, 0}, {3, 3, 0}};
    cache.want(keys);
    cache.waitIdle();
    TEST_CHECK(cache.kept() == 4);
    (void)cache.find(keys[0]); // used just now: kept
    cache.want({{3, 4, 0}});
    cache.waitIdle();
    TEST_CHECK(cache.kept() == 4 && cache.find(keys[0]) != nullptr && cache.find(keys[1]) == nullptr);
    const std::uint64_t before = cache.revision();
    cache.setLayers({"relief", "contours"});
    TEST_CHECK(cache.revision() == before + 1 && cache.kept() == 0);
    cache.want({keys[0]});
    cache.waitIdle();
    const std::shared_ptr<const MapTile> t = cache.find(keys[0]);
    TEST_CHECK(t != nullptr && t->revision == before + 1);
    // An unknown layer: the render throws, nothing kept, the reason told.
    cache.setLayers({"kingdoms"});
    cache.want({keys[0]});
    cache.waitIdle();
    TEST_CHECK(cache.find(keys[0]) == nullptr && !cache.lastError().empty());
}

// The tiles no longer asked for are not rendered; the last probe asked is the one done.
void testStaleAndProbe() {
    const Counted counted;
    *counted.delayMs = 50;
    MapTileCache cache(counted.render(), noProbe(), {"relief"}, {64, 1});
    std::vector<MapTileKey> many;
    for (int i = 0; i < 40; ++i) {
        many.push_back({5, i, 3});
    }
    cache.want(many);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    cache.want({{5, 0, 9}});
    cache.probe(1.0, 2.0);
    cache.probe(100.0, 200.0);
    cache.waitIdle();
    TEST_CHECK(counted.calls->load() < 20); // of the 40 asked first: at most a few started
    TEST_CHECK(cache.find({5, 0, 9}) != nullptr);
    const std::optional<MapProbe> p = cache.takeProbe();
    TEST_CHECK(p.has_value() && p->x == 100.0 && p->z == 200.0 && !cache.takeProbe().has_value());
}

// Destroyed while its threads render: they finish their tile and stop.
void testStop() {
    const Counted counted;
    *counted.delayMs = 50;
    {
        MapTileCache cache(counted.render(), noProbe(), {"relief"}, {64, 3});
        std::vector<MapTileKey> many;
        for (int i = 0; i < 30; ++i) {
            many.push_back({4, i, 0});
        }
        cache.want(many);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    TEST_CHECK(counted.calls->load() <= 6);
}

} // namespace

int main() {
    testRenderAndReuse();
    testCapacityAndLayers();
    testStaleAndProbe();
    testStop();
    return test::result();
}
