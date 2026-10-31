// What the tools of the developers say of a point (Bridge/point_info.hpp, Bridge/ground_ray.hpp):
// the lines of each provider on the small world with its water (and without it), their
// determinism, a provider added by another module, the surface on or off the heightmap, the ray
// of the camera marched to the ground, and the LOD of the tile shown at a chunk.
#include "ground_ray.hpp"
#include "point_info.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"
#include "water_level_support.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::unreal::CorePoint;
using terrain::unreal::InfoLine;
using terrain::unreal::InfoQuery;

const InfoLine* lineOf(const std::vector<InfoLine>& lines, const std::string& label) {
    for (const InfoLine& line : lines) {
        if (line.label == label) {
            return &line;
        }
    }
    return nullptr;
}

bool contains(const std::vector<InfoLine>& lines, const std::string& label,
              const std::string& part) {
    const InfoLine* line = lineOf(lines, label);
    if (line == nullptr || line->value.find(part) == std::string::npos) {
        std::fprintf(stderr, "  %s: \"%s\", expected \"%s\"\n", label.c_str(),
                     line ? line->value.c_str() : "(none)", part.c_str());
        return false;
    }
    return true;
}

// A point of the course of a river of the small world, in its channel.
terrain::RiverPoint riverPoint(const terrain::Hydrology& h) {
    for (std::size_t r = 0; r < h.reachCount(); ++r) {
        const terrain::RiverReach reach = h.reach(r);
        if (reach.points.size() > 8 && reach.falls.empty()) {
            return reach.points[reach.points.size() / 2];
        }
    }
    return {};
}

void testLines(const water_level::World& world) {
    const auto providers = terrain::unreal::defaultInfoProviders();
    const terrain::RiverPoint river = riverPoint(*world.hydrology);
    const InfoQuery query{world.field(), world.tiles.ecology(), nullptr, river.x, river.z, {}};
    const std::vector<InfoLine> lines = terrain::unreal::describePoint(query, providers);
    for (const InfoLine& line : lines) {
        std::printf("  %-8s %-15s %s\n", line.section.c_str(), line.label.c_str(),
                    line.value.c_str());
    }
    for (const char* label : {"x, z", "chunk", "ground", "water", "relief", "biome", "material",
                              "climate", "planet cell", "planet climate", "coast"}) {
        TEST_CHECK(lineOf(lines, label) != nullptr);
    }
    TEST_CHECK(contains(lines, "water", "river at"));
    TEST_CHECK(contains(lines, "valley", "in the channel"));
    TEST_CHECK(lineOf(lines, "altitude") == nullptr && lineOf(lines, "surface") == nullptr);
    TEST_CHECK(lineOf(lines, "tile") == nullptr); // no streamer
    // The same lines every time, and at every image of the point.
    TEST_CHECK(terrain::unreal::describePoint(query, providers) == lines);
    const InfoQuery image{world.field(), world.tiles.ecology(), nullptr,
                          river.x + golden::kSmallPlanet.x,
                          river.z - golden::kSmallPlanet.z, {}};
    TEST_CHECK(terrain::unreal::describePoint(image, providers) == lines);
    // A module adds its lines with one more provider.
    auto more = providers;
    more.push_back([](const InfoQuery&, std::vector<InfoLine>& out) {
        out.push_back({"kingdom", "kingdom", "none yet"});
    });
    const std::vector<InfoLine> extended = terrain::unreal::describePoint(query, more);
    TEST_CHECK(extended.size() == lines.size() + 1 && extended.back().label == "kingdom");
}

void testWithoutPlanet(const water_level::World& world) {
    const terrain::Heightfield& relief = world.relief;
    const terrain::Ecology ecology(water_level::kSeed, terrain::EcologyParams{},
                                   golden::kSmallPlanet);
    const InfoQuery query{relief, ecology, nullptr, 1000.0, -2000.0, {}};
    const auto lines =
        terrain::unreal::describePoint(query, terrain::unreal::defaultInfoProviders());
    TEST_CHECK(contains(lines, "planet", "no planet file"));
    TEST_CHECK(lineOf(lines, "relief") == nullptr && lineOf(lines, "valley") == nullptr);
    TEST_CHECK(lineOf(lines, "nearest fall") == nullptr && lineOf(lines, "biome") != nullptr);
}

void testSurface(const water_level::World& world) {
    const auto providers = terrain::unreal::defaultInfoProviders();
    const double x = 1234.5;
    const double z = -678.25;
    const double ground = world.field().heightAt(x, z);
    InfoQuery query{world.field(), world.tiles.ecology(), nullptr, x, z, ground + 0.1};
    auto lines = terrain::unreal::describePoint(query, providers);
    TEST_CHECK(contains(lines, "surface", "on the heightmap"));
    TEST_CHECK(lineOf(lines, "altitude") != nullptr);
    query.y = ground + 40.0;
    lines = terrain::unreal::describePoint(query, providers);
    TEST_CHECK(contains(lines, "surface", "off the heightmap by +40.00 m"));
}

void testRaysOnShapes() {
    using terrain::unreal::marchGround;
    const auto plane = [](double, double) { return 0.0; };
    const double s = 1.0 / std::sqrt(2.0);
    auto hit = marchGround({0.0, 10.0, 0.0}, {s, -s, 0.0}, 1000.0, plane);
    TEST_CHECK(hit && std::abs(hit->point.x - 10.0) < 0.02);
    TEST_CHECK(hit && std::abs(hit->distance - 14.142) < 0.02);
    TEST_CHECK(hit->point.y == 0.0 && hit->samples < 200);
    // A wall 50 m away, 30 m high, met by a level ray at 10 m.
    const auto wall = [](double x, double) { return x > 50.0 ? 30.0 : 0.0; };
    hit = marchGround({0.0, 10.0, 0.0}, {1.0, 0.0, 0.0}, 1000.0, wall);
    TEST_CHECK(hit && std::abs(hit->point.x - 50.0) < 0.02 && hit->point.y == 30.0);
    // A slope of 1 in 2 far away; a ray up, a ray grazing a plane; an origin under the ground.
    const auto slope = [](double x, double) { return 0.5 * (x - 5000.0); };
    hit = marchGround({0.0, 100.0, 0.0}, {1.0, 0.0, 0.0}, 30000.0, slope);
    TEST_CHECK(hit && std::abs(hit->point.x - 5200.0) < 0.05 && hit->samples < 2000);
    TEST_CHECK(!marchGround({0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, 30000.0, plane));
    TEST_CHECK(!marchGround({0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, 30000.0, plane));
    hit = marchGround({3.0, -1.0, 4.0}, {1.0, 0.0, 0.0}, 100.0, plane);
    TEST_CHECK(hit && hit->distance == 0.0 && hit->point == (CorePoint{3.0, -1.0, 4.0}));
    // The budget of heights is never exceeded: beyond it, no hit.
    TEST_CHECK(!marchGround({0.0, 100.0, 0.0}, {1.0, 0.0, 0.0}, 30000.0, slope, 10));
    // The forward vector of Unreal is the direction of the core with its axes swapped.
    TEST_CHECK(terrain::unreal::coreDirection(0.6, 0.0, -0.8) == (CorePoint{0.6, -0.8, 0.0}));
}

// Rays from 2 m above the ground of the small world, down 10 degrees in 8 directions: each
// hits the ground of the core, and the ray is above it a few centimeters before.
void testRaysOnRelief(const water_level::World& world) {
    const terrain::Heightfield& field = world.field();
    const auto height = [&field](double x, double z) {
        return static_cast<double>(field.heightAt(x, z));
    };
    const double down = std::sin(10.0 * 3.14159265358979 / 180.0);
    const double level = std::cos(10.0 * 3.14159265358979 / 180.0);
    for (int i = 0; i < 8; ++i) {
        const double a = i * 3.14159265358979 / 4.0;
        const CorePoint origin{500.0 * i, height(500.0 * i, -300.0 * i) + 2.0, -300.0 * i};
        const CorePoint direction{level * std::cos(a), -down, level * std::sin(a)};
        const auto hit = terrain::unreal::marchGround(origin, direction, 30000.0, height);
        TEST_CHECK(hit.has_value());
        if (!hit) {
            continue;
        }
        TEST_CHECK(hit->point.y == height(hit->point.x, hit->point.z));
        const double before = hit->distance - 0.02;
        const CorePoint p{origin.x + direction.x * before, origin.y + direction.y * before,
                          origin.z + direction.z * before};
        TEST_CHECK(p.y > height(p.x, p.z) - 1e-6);
    }
}

void testShownTiles() {
    terrain::StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = 1;
    params.maxLoadsPerUpdate = 64;
    const auto generator = [](const terrain::TileRequest& request) {
        terrain::TileData data;
        data.request = request;
        data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
        data.originZ = request.key.origin.z * terrain::kChunkSizeMeters;
        return data;
    };
    terrain::unreal::TileStreamer streamer(generator, params);
    const terrain::unreal::UnrealFrame frame;
    const terrain::unreal::UnrealPoint player{8.0 * 100.0, 8.0 * 100.0, 0.0};
    for (int i = 0; i < 200 && (i < 2 || streamer.manager().pendingCount() > 0); ++i) {
        (void)streamer.generatePending(64);
        (void)streamer.update(player, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    using terrain::unreal::shownTileAt;
    // The tile of the selection of the streamer holding each chunk is the one shown there.
    using terrain::ChunkCoord;
    for (const ChunkCoord chunk : {ChunkCoord{0, 0}, ChunkCoord{3, 0}, ChunkCoord{0, -6},
                                   ChunkCoord{9, 9}}) {
        const terrain::TileKey* expected = nullptr;
        for (const terrain::TileRequest& request : streamer.manager().selection()) {
            expected = terrain::tileContains(request.key, chunk) ? &request.key : expected;
        }
        const auto shown = shownTileAt(streamer, chunk);
        TEST_CHECK(expected != nullptr && shown && *shown == *expected);
    }
    const auto here = shownTileAt(streamer, {0, 0});
    TEST_CHECK(here && here->lod == 0 && here->origin == (terrain::ChunkCoord{0, 0}));
    const auto far = shownTileAt(streamer, {9, 9});
    TEST_CHECK(far && far->lod == 3);
    TEST_CHECK(!shownTileAt(streamer, {100, 100}));
    // Any image of a chunk shows its tile.
    const terrain::ChunkCoord sides = terrain::worldChunks(params.world);
    TEST_CHECK(shownTileAt(streamer, {sides.x, -sides.z}) == here);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    const water_level::World world;
    testLines(world);
    testWithoutPlanet(world);
    testSurface(world);
    testRaysOnShapes();
    testRaysOnRelief(world);
    testShownTiles();
    std::printf("unreal_point_info_test: %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return test::result();
}
