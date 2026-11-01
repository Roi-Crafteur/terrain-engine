// The phase of a load (agent 38, Bridge/load_phase.hpp): the budget of the loads within the cap
// and back to normal, the fade until the ground, the 64 m ring, every tile and the levels 0 to 3
// of the horizon (2 s at most, what was missing then told), the pawn held until its collision,
// the prepared point ("prepare this point, then tell") with the streamer of the plugin, and the
// direction a heading and a pitch look along.
#include "load_phase.hpp"
#include "load_timeline.hpp"
#include "tile_streamer.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileRequest;
using terrain::unreal::LoadBudget;
using terrain::unreal::LoadPhase;
using terrain::unreal::LoadPhaseParams;
using terrain::unreal::LoadStatus;

LoadPhaseParams phaseParams() {
    LoadPhaseParams params;
    params.normal = {2, 2, 8};
    params.cap = 12;
    params.maxUploadsPerFrame = 32;
    params.uploadMillisecondsPerFrame = 6.0;
    params.fadeMaxSeconds = 2.0;
    params.timeoutSeconds = 30.0;
    params.nearHorizonLevels = 4;
    return params;
}

// Everything around shown, with the horizon levels given (of 10).
LoadStatus shown(int horizonLevels) {
    return {true, true, true, true, horizonLevels, 10};
}

void testCap() {
    const LoadBudget normal{2, 2, 8};
    TEST_CHECK(terrain::unreal::defaultLoadingCap(32, normal) == 12); // the 9950X3D
    TEST_CHECK(terrain::unreal::defaultLoadingCap(12, normal) == 8);
    TEST_CHECK(terrain::unreal::defaultLoadingCap(4, normal) == 4); // the normal ones at least
    TEST_CHECK(terrain::unreal::defaultLoadingCap(4, {3, 3, 8}) == 6);
}

// Normal outside a load (the count alone); while loading: half of the cap for the tiles while they
// are pending, then all but the normal workers for the horizon; the uploads within 6 ms.
void testBudget() {
    LoadPhase phase(phaseParams());
    TEST_CHECK((phase.budget(true) == LoadBudget{2, 2, 8, 6.0}));
    phase.begin(0.0);
    TEST_CHECK((phase.budget(true) == LoadBudget{6, 6, 32, 6.0}));
    TEST_CHECK((phase.budget(false) == LoadBudget{2, 10, 32, 6.0}));
    phase.update(shown(10), 0.5); // everything loaded: back to normal
    TEST_CHECK(!phase.loading() && (phase.budget(true) == LoadBudget{2, 2, 8, 6.0}));
}

// The cost of an upload learnt from each upload, per kind (with collision or not): prudent at
// first (above the worst upload measured, 6 ms), up half of the way at once to a heavier upload,
// down a fifth of the way to a lighter one, never below its floor.
void testUploadCost() {
    using terrain::unreal::kMinUploadMilliseconds;
    using terrain::unreal::kStartUploadMillisecondsPerVertex;
    terrain::unreal::UploadCostModel model;
    TEST_CHECK(model.estimate(830, true) > 6.0 && model.estimate(830, false) > 6.0);
    const double before = model.estimate(300, false);
    model.record(0, false, 5.0); // ignored
    model.record(1000, false, 0.0);
    TEST_CHECK(model.estimate(300, false) == before);
    for (int upload = 0; upload < 80; ++upload) {
        model.record(830, false, 0.33); // the spawn: light uploads
    }
    const double light = model.estimate(830, false);
    TEST_CHECK(light >= 0.33 - 1e-9 && light < 0.34);
    TEST_CHECK(model.millisecondsPerVertex(true) == kStartUploadMillisecondsPerVertex);
    model.record(830, false, 6.0); // a heavy one: half of the way at once
    TEST_CHECK(std::abs(model.estimate(830, false) - 0.5 * (light + 6.0)) < 1e-9);
    model.record(830, false, 0.33); // a light one: a fifth of the way only
    TEST_CHECK(model.estimate(830, false) > 2.5);
    TEST_CHECK(model.estimate(0, false) == kMinUploadMilliseconds);
}

// The fade waits for the ground (its collision), the 64 m ring, every tile and the levels 0 to 3;
// the pawn is held until its collision; the boost lasts until every level of the horizon.
void testFadeAndHold() {
    LoadPhase phase(phaseParams());
    phase.begin(10.0);
    TEST_CHECK(phase.loading() && phase.fading() && phase.holding());
    phase.update({true, false, true, true, 4, 10}, 10.2); // no collision yet
    TEST_CHECK(phase.fading() && phase.holding());
    phase.update({true, true, true, true, 3, 10}, 10.4); // level 3 missing
    TEST_CHECK(phase.fading() && !phase.holding());
    phase.update(shown(4), 10.6);
    TEST_CHECK(!phase.fading() && phase.loading() && phase.takeMissingAtCap().empty());
    phase.update(shown(9), 11.0);
    TEST_CHECK(phase.loading());
    phase.update(shown(10), 12.0);
    TEST_CHECK(!phase.loading());
    // The fade does not come back within the same load.
    phase.update(LoadStatus{}, 12.5);
    TEST_CHECK(!phase.fading());
}

// At its cap the fade ends and tells what was missing, once; the pawn stays held until its
// collision, however long it takes; the boost ends at its timeout.
void testFadeCap() {
    LoadPhase phase(phaseParams());
    phase.begin(0.0);
    phase.update({true, false, false, false, 1, 10}, 1.9);
    TEST_CHECK(phase.fading());
    phase.update({true, false, false, false, 1, 10}, 2.0);
    TEST_CHECK(!phase.fading() && phase.holding());
    const std::vector<std::string> missing = phase.takeMissingAtCap();
    TEST_CHECK((missing == std::vector<std::string>{"ground", "64 m", "tiles", "horizon 0-3"}));
    TEST_CHECK(phase.takeMissingAtCap().empty());
    phase.update({true, false, true, true, 10, 10}, 25.0);
    TEST_CHECK(phase.holding() && !phase.loading());
    phase.update({true, true, true, true, 10, 10}, 26.0);
    TEST_CHECK(!phase.holding());
    LoadPhase late(phaseParams());
    late.begin(0.0);
    late.update(LoadStatus{}, 30.0);
    TEST_CHECK(!late.loading());
}

// Without a horizon the fade waits for the tiles only; without the fade nothing is black.
void testWithoutHorizonOrFade() {
    LoadPhase phase(phaseParams());
    phase.begin(0.0);
    phase.update({true, true, true, true, 0, 0}, 0.3);
    TEST_CHECK(!phase.fading() && !phase.loading());
    LoadPhaseParams noFade = phaseParams();
    noFade.fade = false;
    LoadPhase plain(noFade);
    plain.begin(0.0);
    TEST_CHECK(!plain.fading() && plain.holding() && plain.loading());
}

void testInvalid() {
    LoadPhaseParams params = phaseParams();
    params.maxUploadsPerFrame = 4; // below the normal 8
    bool thrown = false;
    try {
        const LoadPhase phase(params);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
    data.originZ = request.key.origin.z * terrain::kChunkSizeMeters;
    data.mesh.vertices = {{static_cast<float>(request.seams), 0.0f, 0.0f},
                          {0.0f, 0.0f, 1.0f},
                          {1.0f, 0.0f, 0.0f}};
    data.mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
    data.mesh.indices = {0, 1, 2};
    return data;
}

terrain::StreamParams streamParams() {
    terrain::StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = 0;
    params.maxLoadsPerUpdate = 8;
    return params;
}

using Shown = std::map<std::tuple<int, int, int>, terrain::unreal::TileUpload>;

terrain::unreal::UnrealPoint pointOf(double x, double z) {
    return terrain::unreal::toUnreal({}, {x, 0.0, z});
}

// The streamer at its focus until the load around it is shown: the uploads, by key, each once.
Shown streamUntilReady(terrain::unreal::TileStreamer& streamer, LoadPhase& phase, double playerX,
                       double playerZ, bool& once) {
    Shown result;
    once = true;
    double now = 0.0;
    for (int frame = 0; frame < 2000; ++frame) {
        const auto [x, z] = phase.prepared().value_or(std::pair(playerX, playerZ));
        streamer.generatePending(4);
        for (terrain::unreal::TileUpload& upload : streamer.update(pointOf(x, z), {}).uploads) {
            const auto key = std::tuple(upload.key.origin.x, upload.key.origin.z, upload.key.lod);
            once = once && !result.contains(key);
            result.insert_or_assign(key, std::move(upload));
        }
        const terrain::unreal::TileRings rings = terrain::unreal::tileRings(
            terrain::unreal::selectedTiles(streamer, terrain::worldToChunk(x, z)));
        const bool all = rings.all && streamer.manager().pendingCount() == 0;
        phase.update({rings.ground, false, rings.ring64, all, 0, 0}, now += 1.0 / 60.0);
        if (phase.takeReady()) {
            return result;
        }
    }
    once = false;
    return result;
}

// "Prepare this point, then tell": the streamer around a point 10 km from the player, told ready
// once, with the very tiles of a streamer that walked there; none around the player; no fade, no
// hold; released, the focus is the player again.
void testPreparedPoint() {
    const double playerX = 100.0;
    const double playerZ = -50.0;
    const double x = 10'100.0;
    const double z = 2'030.0;
    LoadPhase phase(phaseParams());
    phase.prepare(x, z, 0.0);
    TEST_CHECK(!phase.fading() && !phase.holding() && phase.loading());
    terrain::unreal::TileStreamer streamer(fakeTile, streamParams());
    bool once = false;
    const Shown prepared = streamUntilReady(streamer, phase, playerX, playerZ, once);
    TEST_CHECK(once && !prepared.empty());
    TEST_CHECK(!phase.takeReady()); // told once
    const ChunkCoord target = terrain::worldToChunk(x, z);
    for (const auto& [key, upload] : prepared) {
        TEST_CHECK(terrain::tileDistance(upload.key, target) <= 10 + 8);
    }
    LoadPhase walk(phaseParams());
    walk.prepare(x, z, 0.0);
    terrain::unreal::TileStreamer there(fakeTile, streamParams());
    bool onceThere = false;
    const Shown walked = streamUntilReady(there, walk, x, z, onceThere);
    TEST_CHECK(walked.size() == prepared.size());
    bool same = walked.size() == prepared.size();
    for (const auto& [key, upload] : prepared) {
        const auto it = walked.find(key);
        same = same && it != walked.end() && it->second.mesh == upload.mesh &&
               it->second.origin == upload.origin;
    }
    TEST_CHECK(same);
    phase.release();
    TEST_CHECK(!phase.prepared().has_value());
}

// A tile of 4,000 vertices at LOD 0 (2 ms to upload at the first estimate), of 30,000 beyond (15
// ms: more than a budget).
TileData heavyTile(const TileRequest& request) {
    TileData data = fakeTile(request);
    const std::size_t vertices = request.key.lod == 0 ? 4000 : 30000;
    data.mesh.vertices.assign(vertices, {0.0f, 0.0f, 0.0f});
    data.mesh.normals.assign(vertices, {0.0f, 1.0f, 0.0f});
    return data;
}

// The uploads of an update stop before their estimated time goes beyond the budget of 6 ms, one
// at least: three LOD 0 tiles at most once the costs are learnt, a heavier one alone; every tile
// uploaded once.
void testUploadBudget() {
    using terrain::unreal::hasCollision;
    terrain::unreal::UploadCostModel model;
    for (int upload = 0; upload < 100; ++upload) {
        model.record(4000, true, 2.0);
        model.record(30000, false, 15.0);
    }
    terrain::unreal::TileStreamer streamer(heavyTile, streamParams());
    streamer.setMaxLoadsPerUpdate(32);
    streamer.setLoadBudget(
        [&model](const TileData& data) {
            return model.estimate(data.mesh.vertices.size(), hasCollision(data.request.key));
        },
        6.0);
    std::map<std::tuple<int, int, int>, int> uploads;
    bool within = true;
    for (int frame = 0; frame < 2000 && (frame == 0 || streamer.manager().pendingCount() > 0);
         ++frame) {
        streamer.generatePending(64);
        const terrain::unreal::StreamerUpdate update = streamer.update(pointOf(8.0, 8.0), {});
        double estimated = 0.0;
        for (const terrain::unreal::TileUpload& upload : update.uploads) {
            estimated += model.estimate(upload.mesh.positions.size(), upload.collision);
            ++uploads[std::tuple(upload.key.origin.x, upload.key.origin.z, upload.key.lod)];
        }
        within = within && (update.uploads.size() <= 1 || estimated <= 6.0);
    }
    TEST_CHECK(within && streamer.manager().pendingCount() == 0);
    TEST_CHECK(uploads.size() == streamer.manager().selection().size());
    for (const auto& [key, count] : uploads) {
        TEST_CHECK(count == 1);
    }
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

// North, east, south and down: the yaw of Unreal is the heading - 90, a negative pitch looks down.
void testViewAngles() {
    using terrain::unreal::lookDirection;
    using terrain::unreal::viewAngles;
    const terrain::unreal::UnrealPoint north = lookDirection(viewAngles(0.0, 0.0));
    TEST_CHECK(near(north.x, 0.0) && near(north.y, -1.0) && near(north.z, 0.0));
    const terrain::unreal::UnrealPoint east = lookDirection(viewAngles(90.0, 0.0));
    TEST_CHECK(near(east.x, 1.0) && near(east.y, 0.0));
    const terrain::unreal::UnrealPoint south = lookDirection(viewAngles(180.0, 0.0));
    TEST_CHECK(near(south.y, 1.0));
    // The view of the fall from its side: north-west and down.
    const terrain::unreal::ViewAngles fall = viewAngles(316.9, -59.0);
    TEST_CHECK(near(fall.pitch, -59.0) && near(fall.yaw, -133.1));
    const terrain::unreal::UnrealPoint look = lookDirection(fall);
    TEST_CHECK(look.z < -0.85 && look.x < 0.0 && look.y < 0.0);
    TEST_CHECK(near(viewAngles(0.0, -95.0).pitch, -89.9));
    TEST_CHECK(near(viewAngles(270.0, 0.0).yaw, 180.0));
}

} // namespace

int main() {
    testCap();
    testBudget();
    testUploadCost();
    testFadeAndHold();
    testFadeCap();
    testWithoutHorizonOrFade();
    testInvalid();
    testPreparedPoint();
    testUploadBudget();
    testViewAngles();
    return test::result();
}
