// Far horizon of the Unreal plugin, on real levels with small windows: the first epoch shows
// every level, a step rebuilds only the bodies of the levels that moved (vertices only) and the
// trims around them, the simulated meshes of Unreal then match those of a streamer that went
// straight there (with workers or without, across the edge of the world too), a world turn and
// the origin rebasing only move the levels, invalid parameters, and the measures of the log.
#include "horizon_streamer.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using terrain::unreal::HorizonMeshParams;
using terrain::unreal::HorizonMeshStreamer;
using terrain::unreal::HorizonSection;
using terrain::unreal::HorizonUpdate;
using terrain::unreal::HorizonUpload;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealMesh;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;

// Windows of 128 cells, 5 levels (8 m to 128 m), tiles within 23 chunks.
HorizonMeshParams smallParams(int workers) {
    HorizonMeshParams params;
    params.levels = 5;
    params.cellsPerSide = 128;
    params.workerThreads = workers;
    params.lod = {{1, 3, 7}, 23};
    return params;
}

// The meshes of Unreal, as ATerrainWorld keeps them: one component per level holding its
// sections, at the origin of its last upload or move.
class Scene {
public:
    void apply(const HorizonUpdate& update) {
        for (const HorizonUpload& upload : update.uploads) {
            UnrealMesh& mesh = sections_[{upload.level, upload.section}];
            if (upload.newTriangles) {
                mesh = upload.mesh;
            } else {
                vertexOnly_ = vertexOnly_ && upload.section == HorizonSection::Body &&
                              upload.mesh.triangles.empty() &&
                              upload.mesh.positions.size() == mesh.positions.size();
                std::vector<std::int32_t> triangles = std::move(mesh.triangles);
                mesh = upload.mesh;
                mesh.triangles = std::move(triangles);
            }
            origins_[upload.level] = upload.origin;
        }
        for (const terrain::unreal::HorizonMove& move : update.moves) {
            origins_[move.level] = move.origin;
        }
    }

    [[nodiscard]] bool vertexOnly() const { return vertexOnly_; }
    [[nodiscard]] const std::map<int, UnrealPoint>& origins() const { return origins_; }
    [[nodiscard]] bool sameMeshes(const Scene& other) const {
        return sections_ == other.sections_;
    }

private:
    std::map<std::pair<int, HorizonSection>, UnrealMesh> sections_;
    std::map<int, UnrealPoint> origins_;
    bool vertexOnly_ = true;
};

// Updates until no epoch is in flight; every update applied to the scene. Returns the update
// that committed the last epoch.
HorizonUpdate settle(HorizonMeshStreamer& streamer, Scene& scene, const UnrealPoint& player,
                     const UnrealFrame& frame) {
    HorizonUpdate last = streamer.update(player, frame);
    scene.apply(last);
    while (streamer.streamer().busy()) {
        streamer.waitIdle();
        HorizonUpdate next = streamer.update(player, frame);
        scene.apply(next);
        if (next.committed) {
            last = std::move(next);
        }
    }
    return last;
}

struct Session {
    explicit Session(int workers = 0)
        : streamer(world.field, world.ecology, kWorld, smallParams(workers)) {}

    horizon_test::World world;
    HorizonMeshStreamer streamer;
    Scene scene;
};

const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};

void testFirstShow() {
    Session session;
    const HorizonUpdate planned = session.streamer.update({0.0, 0.0, 0.0}, kFrame);
    TEST_CHECK(planned.empty() && !planned.committed && session.streamer.streamer().busy());
    TEST_CHECK(!session.streamer.shown());
    session.streamer.waitIdle();
    const HorizonUpdate first = session.streamer.update({0.0, 0.0, 0.0}, kFrame);
    TEST_CHECK(first.committed && first.first && first.coarsestMoved == 4);
    TEST_CHECK(first.samples == 5 * 129 * 129);
    TEST_CHECK(first.uploads.size() == 9 && first.moves.empty());
    const terrain::unreal::CorePoint observer = terrain::unreal::toCore(kFrame, {0.0, 0.0, 0.0});
    for (const HorizonUpload& upload : first.uploads) {
        const terrain::HorizonLevel& level = session.streamer.streamer().level(upload.level);
        TEST_CHECK(upload.origin == terrain::unreal::levelOrigin(level, kFrame, observer));
        TEST_CHECK(upload.newTriangles && !upload.mesh.empty());
    }
    TEST_CHECK(std::count_if(first.uploads.begin(), first.uploads.end(), [](const HorizonUpload& u) {
                   return u.section == HorizonSection::Trim;
               }) == 4);
    TEST_CHECK(session.streamer.shown());
    TEST_CHECK(session.streamer.memoryBytes() > session.streamer.streamer().clipmap().memoryBytes());
    // Nothing changes at the same place.
    TEST_CHECK(session.streamer.update({0.0, 0.0, 0.0}, kFrame).empty());
}

// Levels whose window moves between two observers.
std::vector<int> movedLevels(const terrain::HorizonParams& params, double x0, double z0,
                             double x1, double z1) {
    std::vector<int> moved;
    for (int n = 0; n < params.levels; ++n) {
        if (!(terrain::canonicalCenter(params, n, x0, z0) ==
              terrain::canonicalCenter(params, n, x1, z1))) {
            moved.push_back(n);
        }
    }
    return moved;
}

void testSteps() {
    Session session;
    settle(session.streamer, session.scene, {0.0, 0.0, 0.0}, kFrame);
    const terrain::HorizonParams& params = session.streamer.params();
    UnrealPoint player{0.0, 0.0, 0.0};
    bool exact = true;
    for (const double step : {2000.0, 1500.0, 3300.0, 6400.0, 25'000.0}) {
        const terrain::unreal::CorePoint from = terrain::unreal::toCore(kFrame, player);
        player.x += step;
        player.y -= 0.4 * step;
        const terrain::unreal::CorePoint to = terrain::unreal::toCore(kFrame, player);
        const std::vector<int> moved = movedLevels(params, from.x, from.z, to.x, to.z);
        const HorizonUpdate update = settle(session.streamer, session.scene, player, kFrame);
        std::vector<int> bodies;
        std::vector<int> trims;
        for (const HorizonUpload& upload : update.uploads) {
            (upload.section == HorizonSection::Body ? bodies : trims).push_back(upload.level);
            exact = exact && (upload.section == HorizonSection::Trim) == upload.newTriangles;
        }
        TEST_CHECK(bodies == moved);
        TEST_CHECK(update.coarsestMoved == (moved.empty() ? -1 : moved.back()));
        std::vector<int> expectedTrims;
        for (int n = 1; n < params.levels; ++n) {
            if (std::find(moved.begin(), moved.end(), n) != moved.end() ||
                std::find(moved.begin(), moved.end(), n - 1) != moved.end()) {
                expectedTrims.push_back(n);
            }
        }
        TEST_CHECK(trims == expectedTrims);
        TEST_CHECK(update.moves.empty());
    }
    TEST_CHECK(exact);
    TEST_CHECK(session.scene.vertexOnly());
    // The meshes after the walk are those of a streamer that went straight there.
    Session straight;
    settle(straight.streamer, straight.scene, player, kFrame);
    TEST_CHECK(session.scene.sameMeshes(straight.scene));
    TEST_CHECK(session.scene.origins() == straight.scene.origins());
}

// A world turn moves the player by whole worlds: the levels move by as much, nothing else.
void testWorldTurn() {
    Session session;
    settle(session.streamer, session.scene, {0.0, 0.0, 0.0}, kFrame);
    const std::map<int, UnrealPoint> before = session.scene.origins();
    const UnrealPoint turned{2.0 * kWorld.x * 100.0, -kWorld.z * 100.0, 0.0};
    const HorizonUpdate update = session.streamer.update(turned, kFrame);
    TEST_CHECK(update.uploads.empty() && !update.committed && update.moves.size() == 5);
    TEST_CHECK(!session.streamer.streamer().busy());
    session.scene.apply(update);
    bool shifted = true;
    for (const auto& [level, origin] : session.scene.origins()) {
        const UnrealPoint& old = before.at(level);
        shifted = shifted && origin.x == old.x + turned.x && origin.y == old.y + turned.y &&
                  origin.z == old.z;
    }
    TEST_CHECK(shifted);
    // The origin rebasing of Unreal moves the frame: the levels follow.
    const UnrealPoint offset{-1'000'000.0, 500'000.0, 0.0};
    const UnrealFrame rebased = terrain::unreal::shifted(kFrame, offset);
    const UnrealPoint player{turned.x + offset.x, turned.y + offset.y, 0.0};
    const HorizonUpdate moved = session.streamer.update(player, rebased);
    TEST_CHECK(moved.uploads.empty() && moved.moves.size() == 5);
    const std::map<int, UnrealPoint> turnedOrigins = session.scene.origins();
    session.scene.apply(moved);
    bool followed = true;
    for (const auto& [level, origin] : session.scene.origins()) {
        const UnrealPoint& old = turnedOrigins.at(level);
        followed = followed && std::abs(origin.x - (old.x + offset.x)) < 1e-3 &&
                   std::abs(origin.y - (old.y + offset.y)) < 1e-3;
    }
    TEST_CHECK(followed);
}

// A walk across the east edge of the world, with workers: the origins stay continuous, and the
// meshes end as those of a streamer started at the end, without workers.
void testEdgeWalk() {
    const UnrealFrame frame{{kWorld.x / 2.0 - 300.0, 50.0, 1'000'000.0}};
    Session walker(2);
    std::map<int, UnrealPoint> previous;
    bool continuous = true;
    UnrealPoint player{0.0, 0.0, 0.0};
    for (int step = 0; step <= 60; ++step) {
        player.x = step * 1'000.0;
        settle(walker.streamer, walker.scene, player, frame);
        for (const auto& [level, origin] : walker.scene.origins()) {
            const auto found = previous.find(level);
            const double pitchCm = 100.0 * terrain::levelPitch(walker.streamer.params(), level);
            continuous = continuous && (found == previous.end() ||
                                        std::abs(origin.x - found->second.x) <= 1'000.0 +
                                                                                 2.0 * pitchCm);
        }
        previous = walker.scene.origins();
    }
    TEST_CHECK(continuous);
    TEST_CHECK(terrain::unreal::toCore(frame, player).x > kWorld.x / 2.0);
    Session straight;
    settle(straight.streamer, straight.scene, player, frame);
    TEST_CHECK(walker.scene.sameMeshes(straight.scene));
    TEST_CHECK(walker.scene.origins() == straight.scene.origins());
}

void testInvalid() {
    const horizon_test::World world;
    HorizonMeshParams params = smallParams(0);
    params.lod.viewDistance = 24; // the tiles reach beyond level 0
    bool refused = false;
    try {
        [[maybe_unused]] const HorizonMeshStreamer streamer(world.field, world.ecology, kWorld,
                                                            params);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
    params = smallParams(-1);
    refused = false;
    try {
        [[maybe_unused]] const HorizonMeshStreamer streamer(world.field, world.ecology, kWorld,
                                                            params);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
    params = smallParams(0);
    params.planetRadiusMeters = 6'371'000.0;
    const HorizonMeshStreamer earth(world.field, world.ecology, kWorld, params);
    TEST_CHECK(earth.planetRadiusMeters() == 6'371'000.0);
    TEST_CHECK((earth.tileHole() == terrain::unreal::TileHole{29, 128.0}));
    TEST_CHECK(earth.params().crestRatio == terrain::HorizonParams{}.crestRatio);
    params.crestRatio = 0; // the point samples
    const HorizonMeshStreamer points(world.field, world.ecology, kWorld, params);
    TEST_CHECK(points.params().crestRatio == 0);
}

bool contains(const std::vector<std::string>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(), [&text](const std::string& line) {
        return line.find(text) != std::string::npos;
    });
}

void testTimings() {
    terrain::unreal::HorizonTimings timings;
    TEST_CHECK(terrain::unreal::describe(timings.snapshot()).empty());
    timings.recordTick(-1, false, 0.5);
    timings.recordTick(0, false, 4.0);
    timings.recordTick(0, false, 6.0);
    timings.recordTick(9, true, 80.0);
    timings.recordUpload(HorizonSection::Body, 1.0, 2.0, 1000);
    timings.recordUpload(HorizonSection::Body, 1.0, 2.0, 3000);
    timings.recordEpoch(10.0, 500);
    const std::vector<std::string> lines = terrain::unreal::describe(timings.take());
    TEST_CHECK(lines.size() == 3);
    TEST_CHECK(contains(lines, "horizon ticks: first show 1 ticks 80.00 ms (max 80.00), level 0 "
                               "moves 2 ticks 5.00 ms (max 6.00), without epoch 1 ticks 0.50 ms"));
    TEST_CHECK(contains(lines, "horizon body: conversion 2 meshes 1.00 ms (max 1.00), upload 2 "
                               "meshes 2.00 ms (max 2.00), 2000 vertices per mesh"));
    TEST_CHECK(contains(lines, "horizon epochs: 1 epochs 10.00 ms (max 10.00) from plan to commit, "
                               "500 samples"));
    TEST_CHECK(terrain::unreal::describe(timings.snapshot()).empty());
    bool refused = false;
    try {
        timings.recordTick(terrain::kHorizonMaxLevels, false, 1.0);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

} // namespace

int main() {
    testFirstShow();
    testSteps();
    testWorldTurn();
    testEdgeWalk();
    testInvalid();
    testTimings();
    return test::result();
}
