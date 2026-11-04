// The tiles of the dev map on threads of their own (Bridge/map_tile_cache.hpp): the bytes of
// renderMap, kept and reused (moving a view renders only its new tiles), the least used dropped
// beyond the capacity, the stale requests dropped, the layers changed, the probe of the cursor,
// the landing of a click (the last asked, as clickLandingOn the source gives it), a clean stop
// while tiles render, and a stop in the middle of heavy tiles of the generated world that does
// not wait for their end: every render in flight stops before it (between two rows: MapCancelled),
// the tile dropped, in a small part of the time these tiles would still have taken.
#include "map_tile_cache.hpp"

#include "map_test_support.hpp"
#include "map_view.hpp"
#include "map_world_support.hpp"
#include "test_assert.hpp"

#include "terrain/map/map_style.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
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
        return [c, d](const MapTileKey& key, const std::vector<std::string>& layers,
                      MapCancel cancel) {
            ++*c;
            std::this_thread::sleep_for(std::chrono::milliseconds(d->load()));
            static const map_test::SyntheticSampler sampler;
            static const MapLayers registry = defaultMapLayers();
            return renderMap(sampler, mapTileFrame(key), registry, layers, {1, kMapMaxSamples, cancel});
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

// The landing of a click on the threads of the cache, before the tiles: the last asked is the one
// done, and it is clickLandingOn the heightfield of the source.
void testLanding() {
    const Counted counted;
    *counted.delayMs = 20;
    const map_world::DryWorld w;
    MapTileCache cache(counted.render(), noProbe(), {"relief"}, {64, 1}, landingsOf(w.source));
    std::vector<MapTileKey> many;
    for (int i = 0; i < 10; ++i) {
        many.push_back({6, i, 2});
    }
    cache.want(many);
    cache.land(-2699.0, -1697.0, 50.0);
    cache.land(-3985.0, -165.0, 50.0); // the beach of the origin
    cache.waitIdle();
    const std::optional<MapLanding> l = cache.takeLanding();
    const MapLanding direct = clickLandingOn(w.field, -3985.0, -165.0, 50.0);
    TEST_CHECK(l.has_value() && l->clickX == -3985.0 && l->clickZ == -165.0);
    TEST_CHECK(l->kind == direct.kind && l->x == direct.x && l->z == direct.z);
    TEST_CHECK(!cache.takeLanding().has_value());
    MapTileCache without(counted.render(), noProbe(), {"relief"}, {64, 1});
    without.land(0.0, 0.0, 50.0); // no landing function: nothing asked
    without.waitIdle();
    TEST_CHECK(!without.takeLanding().has_value());
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

// The renders of a cache, counted: started, stopped before their end once the stop was asked (by
// MapCancelled between two rows, or by any other exception of the stop, as one raised in the
// middle of the courses of the hydrology), and those that ran to their end once the stop was
// asked (the stop then waited for them).
struct RenderOutcomes {
    std::atomic<int> started{0};
    std::atomic<int> stopped{0};
    std::atomic<int> finishedAfterStop{0};
    std::atomic<bool> stopAsked{false};
};

MapTileCache::Render countedRender(MapTileCache::Render render,
                                   const std::shared_ptr<RenderOutcomes>& outcomes) {
    return [render = std::move(render), outcomes](const MapTileKey& key,
                                                  const std::vector<std::string>& layers,
                                                  MapCancel cancel) {
        ++outcomes->started;
        try {
            MapImage image = render(key, layers, cancel);
            if (outcomes->stopAsked.load()) {
                ++outcomes->finishedAfterStop;
            }
            return image;
        } catch (const MapCancelled&) {
            ++outcomes->stopped;
            throw;
        } catch (...) {
            if (outcomes->stopAsked.load()) {
                ++outcomes->stopped;
            }
            throw;
        }
    };
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

// Two tiles of 2 and two of 64 m per pixel around a fall of the small world: the heaviest, on a
// hydrology that has computed nothing yet (their courses to compute).
std::vector<MapTileKey> heavyTiles() {
    const map_world::FallPlace fall = map_world::firstFall();
    std::vector<MapTileKey> heavy;
    for (const int level : {1, 6}) {
        const MapTileKey k = mapTileAt(level, fall.x, fall.z, golden::kSmallPlanet);
        heavy.push_back(k);
        heavy.push_back({level, k.tx + 1, k.tz});
    }
    return heavy;
}

// The time the cache takes to render the heavy tiles to their end, on a new world, with the
// threads of the stops below.
double millisecondsToFinish(const std::vector<MapTileKey>& heavy) {
    const map_world::WetWorld w;
    MapTileCache cache(renderTilesOf(w.source), probesOf(w.source), defaultMapLayers().names(),
                       MapCacheParams{64, 2});
    const auto start = std::chrono::steady_clock::now();
    cache.want(heavy);
    cache.waitIdle();
    const double ms = millisecondsSince(start);
    TEST_CHECK(cache.kept() == heavy.size());
    return ms;
}

// The cache of the small world with its water, destroyed 40 ms after its threads started on the
// heavy tiles: no render runs to its end, each one in flight stops before it; the best of
// 3 stops takes less than a quarter of the time these tiles would still have taken (measured in
// this test, on this machine as it is loaded now).
void testStopHeavyTiles() {
    const std::vector<MapTileKey> heavy = heavyTiles();
    double best = 1.0e9;
    double longestBeforeStop = 0.0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const map_world::WetWorld w;
        const auto outcomes = std::make_shared<RenderOutcomes>();
        auto cache = std::make_unique<MapTileCache>(countedRender(renderTilesOf(w.source), outcomes),
                                                    probesOf(w.source), defaultMapLayers().names(),
                                                    MapCacheParams{64, 2});
        const auto asked = std::chrono::steady_clock::now();
        cache->want(heavy);
        while (outcomes->started.load() == 0 && millisecondsSince(asked) < 10000.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const bool busy = cache->waiting() > 0 && cache->kept() == 0;
        longestBeforeStop = std::max(longestBeforeStop, millisecondsSince(asked));
        outcomes->stopAsked = true;
        const auto start = std::chrono::steady_clock::now();
        cache.reset();
        const double ms = millisecondsSince(start);
        std::printf("cache stopped in the middle of heavy tiles in %.1f ms: %d renders started, "
                    "%d stopped before their end, %d ran to their end\n",
                    ms, outcomes->started.load(), outcomes->stopped.load(),
                    outcomes->finishedAfterStop.load());
        TEST_CHECK(busy);
        TEST_CHECK(outcomes->started.load() >= 1 && outcomes->stopped.load() >= 1);
        TEST_CHECK(outcomes->finishedAfterStop.load() == 0);
        best = std::min(best, ms);
    }
    const double finish = millisecondsToFinish(heavy);
    const double remaining = finish - longestBeforeStop;
    std::printf("the heavy tiles to their end: %.1f ms, %.1f ms left at the stop; best stop "
                "%.1f ms (%.3f of it)\n",
                finish, remaining, best, best / remaining);
    TEST_CHECK(best < remaining / 4.0);
}

} // namespace

int main() {
    testRenderAndReuse();
    testCapacityAndLayers();
    testStaleAndProbe();
    testLanding();
    testStop();
    testStopHeavyTiles();
    return test::result();
}
