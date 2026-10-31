// The session of the plugin built on another thread (agent 38, TerrainWorldStart.cpp builds it
// there at the start, behind the black screen): the same world as one built on the calling
// thread, whichever is built first (the planetary map shared by the process, Geography::shared):
// the same frame, the same heights, the same tiles bit for bit; and the check of the world on a
// thread finds the world of terrain_core.
#include "determinism_check.hpp"
#include "terrain_session.hpp"

#include "test_assert.hpp"

#include <future>
#include <memory>
#include <vector>

namespace {

using terrain::TileRequest;
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

} // namespace

int main() {
    testSameWorld();
    testCheckOnThread();
    return test::result();
}
