// The session of the plugin built on another thread (agent 38, TerrainWorldStart.cpp builds it
// there at the start, behind the black screen): the same world as one built on the calling
// thread, whichever is built first (the planetary map shared by the process, Geography::shared):
// the same frame, the same heights, the same tiles bit for bit; and the check of the world on a
// thread finds the world of terrain_core.
#include "determinism_check.hpp"
#include "ground_read.hpp"
#include "terrain_session.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

using terrain::TileRequest;
using terrain::unreal::GroundLevel;
using terrain::unreal::GroundRead;
using terrain::unreal::SessionParams;
using terrain::unreal::TerrainSession;

SessionParams smallSession() {
    SessionParams params;
    params.stream.lod = {{2, 4, 8}, 6};
    params.stream.workerThreads = 2;
    return params;
}

std::unique_ptr<TerrainSession> buildOnThread(const SessionParams& params) {
    return std::async(std::launch::async,
                      [params] { return std::make_unique<TerrainSession>(params); })
        .get();
}

bool sameWorld(const TerrainSession& a, const TerrainSession& b) {
    bool same = a.frameAt(1234.5, -987.25) == b.frameAt(1234.5, -987.25);
    for (const double x : {0.0, 162047.75, -147456.25, 163296.25}) {
        same = same && a.tiles().heightfield().heightAt(x, 0.25 * x) ==
                           b.tiles().heightfield().heightAt(x, 0.25 * x);
    }
    const std::vector<TileRequest> requests = {{{{0, 0}, 0}, 0},
                                               {{{10128, 11760}, 0}, 0},
                                               {{{-9216, 3584}, 1}, terrain::kSeamNegX}};
    for (const TileRequest& request : requests) {
        same = same && a.tiles().generate(request) == b.tiles().generate(request);
    }
    return same;
}

// Built on a thread first (it builds the planetary map), then on this one (which finds it), and
// the other way round in a second pair.
void testSameWorld() {
    const std::unique_ptr<TerrainSession> there = buildOnThread(smallSession());
    const TerrainSession here(smallSession());
    TEST_CHECK(sameWorld(*there, here));
    SessionParams other = smallSession();
    other.seed = terrain::kDefaultWorldSeed + 1;
    const TerrainSession hereFirst(other);
    const std::unique_ptr<TerrainSession> thereAfter = buildOnThread(other);
    TEST_CHECK(sameWorld(hereFirst, *thereAfter));
}

void testCheckOnThread() {
    const terrain::unreal::DeterminismReport report =
        std::async(std::launch::async, [] { return terrain::unreal::checkDefaultWorld(); }).get();
    TEST_CHECK(report.same);
}

// The ground of a teleport read on a thread (ground_read.hpp): the level of the last point asked,
// given once, the same as read on this thread; a read asked again or forgotten is never given.
void testGroundRead() {
    const TerrainSession session(smallSession());
    const terrain::TerrainTiles& field = session.tiles();
    GroundRead read;
    TEST_CHECK(!read.pending() && !read.poll().has_value());
    read.ask(field, 1000.0, -2000.0);
    read.ask(field, 162047.75, 40511.9375); // the first read forgotten
    std::optional<GroundLevel> level;
    for (int wait = 0; wait < 20000 && !level.has_value(); ++wait) {
        level = read.poll();
        if (!level.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const GroundLevel here = terrain::unreal::groundLevelAt(field, 162047.75, 40511.9375);
    TEST_CHECK(level.has_value() && level->x == here.x && level->z == here.z);
    TEST_CHECK(level.has_value() && level->ground == here.ground && level->stand == here.stand);
    TEST_CHECK(!read.pending() && !read.poll().has_value());
    read.ask(field, 0.0, 0.0);
    read.forget();
    TEST_CHECK(!read.pending() && !read.poll().has_value());
}

// The level to stand at: the ground on dry land (the top of the rock of the tiles), the surface of
// the water over it elsewhere (the sea, a lake, a river): a pawn placed there is never under the
// water.
void testStandLevel() {
    const TerrainSession session(smallSession());
    const terrain::Heightfield& field = session.tiles().heightfield();
    int wet = 0;
    int dry = 0;
    for (int i = 0; i < 24; ++i) {
        const double x = -1.5e6 + 125000.0 * i;
        const double z = 0.37 * x + 2500.0;
        const terrain::HeightSample sample = field.sampleAt(x, z);
        const GroundLevel level = terrain::unreal::groundLevelAt(session.tiles(), x, z);
        TEST_CHECK(level.ground == static_cast<double>(session.tiles().groundAt(x, z)));
        TEST_CHECK(level.stand >= level.ground);
        if (sample.water != terrain::WaterKind::None &&
            static_cast<double>(sample.waterLevel) > level.ground) {
            ++wet;
            TEST_CHECK(level.stand == static_cast<double>(sample.waterLevel));
        } else {
            ++dry;
            TEST_CHECK(level.stand == level.ground);
        }
    }
    TEST_CHECK(wet > 0 && dry > 0);
}

} // namespace

int main() {
    testSameWorld();
    testCheckOnThread();
    testGroundRead();
    testStandLevel();
    return test::result();
}
