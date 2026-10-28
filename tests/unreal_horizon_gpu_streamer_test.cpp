// GPU horizon of the Unreal plugin (Bridge/horizon_gpu_streamer, Bridge/horizon_morph), on real
// levels with small windows: the first epoch writes every texel and places every level, a step
// only writes the bands of the levels that moved, the simulated textures and placements after a
// walk (across the edge of the world too) equal those of a streamer that went straight there, a
// world turn only moves the levels, the grids, and the geomorphing: its factor (0 inside, 1 on
// the border wherever the viewer stands, continuous) and its target, the surface of the coarser
// level read in its texels at every sample of the finer one, with the crests and with point
// samples (then as before them, bit for bit); the log line of the texel uploads.
#include "horizon_gpu_streamer.hpp"
#include "horizon_morph.hpp"
#include "horizon_timings.hpp"

#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using terrain::HorizonIndex;
using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::kTexelFloats;
using terrain::unreal::LevelPlacement;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;
const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};

// Windows of 128 cells, 5 levels, tiles within 23 chunks, jobs run on the calling thread.
HorizonGpuParams smallParams() {
    HorizonGpuParams params;
    params.levels = 5;
    params.cellsPerSide = 128;
    params.lod = {{1, 3, 7}, 23};
    return params;
}

using gpu_test::Gpu;

struct Session {
    explicit Session(const HorizonGpuParams& params = smallParams(),
                     const terrain::HeightfieldParams& relief = {})
        : world(relief), streamer(world.field, world.ecology, kWorld, params,
                                  [](std::function<void()> job) { job(); }) {}

    horizon_test::World world;
    HorizonGpuStreamer streamer;
    Gpu gpu{129};
};

// The texels of every window are those of the samples of the clipmap raised to their crests: the
// samples of a sampler with the crests in the epochs.
bool texelsMatch(const Session& session) {
    const terrain::HorizonSampler reference(session.world.field, session.world.ecology,
                                            session.streamer.params());
    return gpu_test::texelsMatch(session.gpu, session.streamer.clipmap(), reference);
}

void testFirstAndSteps() {
    Session session;
    const HorizonGpuUpdate first = session.streamer.update({0.0, 0.0, 0.0}, kFrame);
    TEST_CHECK(first.committed && first.first && first.coarsestMoved == 4);
    TEST_CHECK(first.placements.size() == 5u);
    std::size_t texels = 0;
    for (const auto& region : first.regions) {
        texels += region.data.size() / kTexelFloats;
    }
    // Every texel, and those of the samples that rise to their crests again (the jobs run within
    // the update here).
    TEST_CHECK(texels >= 5u * 129u * 129u);
    session.gpu.apply(first);
    TEST_CHECK(texelsMatch(session));
    TEST_CHECK(session.streamer.update({0.0, 0.0, 0.0}, kFrame).empty());
    UnrealPoint player{0.0, 0.0, 0.0};
    bool bands = true;
    for (const double step : {1500.0, 2600.0, 9000.0, 33'000.0}) {
        const terrain::unreal::CorePoint from = terrain::unreal::toCore(kFrame, player);
        player.x += step;
        player.y -= 0.6 * step;
        const terrain::unreal::CorePoint to = terrain::unreal::toCore(kFrame, player);
        std::set<int> moved;
        for (int n = 0; n < 5; ++n) {
            if (!(terrain::canonicalCenter(session.streamer.params(), n, from.x, from.z) ==
                  terrain::canonicalCenter(session.streamer.params(), n, to.x, to.z))) {
                moved.insert(n);
            }
        }
        const HorizonGpuUpdate update = session.streamer.update(player, kFrame);
        for (const auto& region : update.regions) {
            bands = bands && moved.contains(region.level) &&
                    region.data.size() < 129u * 129u * kTexelFloats;
        }
        for (const LevelPlacement& placement : update.placements) {
            bands = bands && (moved.contains(placement.level) ||
                              moved.contains(placement.level - 1) ||
                              moved.contains(placement.level + 1));
        }
        session.gpu.apply(update);
    }
    TEST_CHECK(bands);
    TEST_CHECK(texelsMatch(session));
    Session straight;
    straight.gpu.apply(straight.streamer.update(player, kFrame));
    TEST_CHECK(session.gpu.sameView(straight.gpu));
}

// A world turn moves the player by whole worlds: the levels only move by as much.
void testWorldTurn() {
    Session session;
    session.gpu.apply(session.streamer.update({0.0, 0.0, 0.0}, kFrame));
    const std::map<int, LevelPlacement> before = session.gpu.placements();
    const UnrealPoint turned{kWorld.x * 100.0, 2.0 * kWorld.z * 100.0, 0.0};
    const HorizonGpuUpdate update = session.streamer.update(turned, kFrame);
    TEST_CHECK(update.regions.empty() && !update.committed && update.placements.size() == 5u);
    bool shifted = true;
    for (const LevelPlacement& placement : update.placements) {
        const LevelPlacement& old = before.at(placement.level);
        shifted = shifted && placement.origin.x == old.origin.x + turned.x &&
                  placement.origin.y == old.origin.y + turned.y &&
                  placement.firstSlot == old.firstSlot && placement.trim == old.trim;
    }
    TEST_CHECK(shifted);
}

// Across the east edge of the world: the view equals the one of a streamer started at the end.
void testEdge() {
    const UnrealFrame frame{{kWorld.x / 2.0 - 400.0, 10.0, 2'000'000.0}};
    Session walker;
    UnrealPoint player{0.0, 0.0, 0.0};
    for (int step = 0; step <= 40; ++step) {
        player.x = step * 2'000.0;
        walker.gpu.apply(walker.streamer.update(player, frame));
    }
    TEST_CHECK(terrain::unreal::toCore(frame, player).x > kWorld.x / 2.0);
    Session straight;
    straight.gpu.apply(straight.streamer.update(player, frame));
    TEST_CHECK(walker.gpu.sameView(straight.gpu));
}

void testGrids() {
    const Session session;
    const auto level0 = session.streamer.level0Grid();
    const auto ring = session.streamer.ringGrid();
    TEST_CHECK(!level0.vertices.empty() && !ring.vertices.empty());
    TEST_CHECK(level0.vertices.back().kind == terrain::unreal::GridVertexKind::Sea);
    TEST_CHECK(ring.vertices.back().kind == terrain::unreal::GridVertexKind::Skirt);
    // The sea square reaches the guaranteed coverage of the tiles: 360 m around the center.
    TEST_CHECK(level0.vertices.back().i == 64.0f + 45.0f);
    TEST_CHECK(session.streamer.skirtMetersOf(0) == 2.0 && session.streamer.skirtMetersOf(4) == 8.0);
    TEST_CHECK(session.streamer.curveMetersOf(4) > session.streamer.curveMetersOf(3));
}

void testMorphFactor() {
    constexpr int kCells = 128;
    constexpr double kWidth = 16.0;
    TEST_CHECK(terrain::unreal::morphFactor(64.0, 64.0, 64.0, 64.0, kCells, kWidth) == 0.0);
    TEST_CHECK(terrain::unreal::morphFactor(64.0 + 46.0, 64.0, 64.0, 64.0, kCells, kWidth) == 0.0);
    TEST_CHECK(terrain::unreal::morphFactor(64.0 + 54.0, 64.0, 64.0, 64.0, kCells, kWidth) == 0.5);
    bool border = true;
    for (const double u : {62.5, 63.0, 64.0, 65.0, 65.5}) {
        for (int j = 0; j <= kCells; ++j) {
            for (const auto& [i, k] : {std::pair{0, j}, std::pair{kCells, j}, std::pair{j, 0},
                                       std::pair{j, kCells}}) {
                border = border && terrain::unreal::morphFactor(i, k, u, 64.0, kCells, kWidth) == 1.0;
            }
        }
    }
    TEST_CHECK(border);
    // Continuous as the viewer moves: no jump when the window moves by two cells.
    double previous = terrain::unreal::morphFactor(120.0, 70.0, 60.0, 64.0, kCells, kWidth);
    bool smooth = true;
    for (double u = 60.0; u <= 68.0; u += 0.01) {
        const double factor = terrain::unreal::morphFactor(120.0, 70.0, u, 64.0, kCells, kWidth);
        smooth = smooth && std::abs(factor - previous) <= 0.01 / kWidth + 1e-12;
        previous = factor;
    }
    TEST_CHECK(smooth);
}

// The height of the coarser level under the sample (i, k) of the finer one: its triangles.
double coarseSurface(const Session& session, int fine, int i, int k) {
    const LevelPlacement& placement = session.gpu.placements().at(fine);
    const double u = static_cast<double>(placement.inCoarser.x) + i / 2.0;
    const double v = static_cast<double>(placement.inCoarser.z) + k / 2.0;
    const int ci = static_cast<int>(std::floor(u));
    const int ck = static_cast<int>(std::floor(v));
    const double s = u - ci;
    const double t = v - ck;
    const auto h = [&](int a, int b) {
        return static_cast<double>(session.gpu.texel(fine + 1, a, b).height);
    };
    if (s + t <= 1.0) {
        return h(ci, ck) + s * (h(ci + 1, ck) - h(ci, ck)) + t * (h(ci, ck + 1) - h(ci, ck));
    }
    return h(ci + 1, ck + 1) + (1.0 - s) * (h(ci, ck + 1) - h(ci + 1, ck + 1)) +
           (1.0 - t) * (h(ci + 1, ck) - h(ci + 1, ck + 1));
}

// The morph target of every sample of every finer level lies on the coarser surface, read in
// the coarser texels: the even samples bit for bit, the others within the rounding of a float
// mean. Around mountains (the legacy relief), with the crests and with point samples: with
// point samples the target is the mean of the level's own texels as before, bit for bit; with
// the crests the coarser texels differ from the even texels of the finer level at some samples.
void testMorphTarget(int crestRatio) {
    HorizonGpuParams params = smallParams();
    params.crestRatio = crestRatio;
    Session session(params, test_relief::legacyRelief());
    session.gpu.apply(session.streamer.update({0.0, 0.0, 0.0}, kFrame));
    TEST_CHECK(session.streamer.params().crestRatio == crestRatio);
    bool exact = true;
    bool asBefore = true;
    int lifted = 0;
    double worst = 0.0;
    for (int fine = 0; fine + 1 < 5; ++fine) {
        const LevelPlacement& placement = session.gpu.placements().at(fine);
        const auto coarse = [&](int i, int k) {
            return session.gpu
                .texel(fine + 1, static_cast<int>(placement.inCoarser.x) + i / 2,
                       static_cast<int>(placement.inCoarser.z) + k / 2)
                .height;
        };
        for (int k = 0; k <= 128; ++k) {
            for (int i = 0; i <= 128; ++i) {
                const terrain::unreal::MorphSources from = terrain::unreal::morphSources(i, k);
                const float target = 0.5f * (coarse(from.i0, from.k0) + coarse(from.i1, from.k1));
                const float own = 0.5f * (session.gpu.texel(fine, from.i0, from.k0).height +
                                          session.gpu.texel(fine, from.i1, from.k1).height);
                const double expected = coarseSurface(session, fine, i, k);
                asBefore = asBefore && target == own;
                if (i % 2 == 0 && k % 2 == 0) {
                    exact = exact && static_cast<double>(target) == expected;
                    lifted += own == target ? 0 : 1;
                } else {
                    worst = std::max(worst, std::abs(target - expected));
                }
            }
        }
    }
    std::printf("morph targets, crest ratio %d: %d even samples morph toward lifted coarser "
                "texels\n",
                crestRatio, lifted);
    TEST_CHECK(exact);
    TEST_CHECK(worst < 1e-3);
    TEST_CHECK(crestRatio == 0 ? asBefore : lifted > 0);
    TEST_CHECK((terrain::unreal::morphSources(3, 5) == terrain::unreal::MorphSources{4, 4, 2, 6}));
    TEST_CHECK((terrain::unreal::morphSources(3, 4) == terrain::unreal::MorphSources{2, 4, 4, 4}));
    TEST_CHECK((terrain::unreal::morphSources(2, 5) == terrain::unreal::MorphSources{2, 4, 2, 6}));
}

// The texel uploads have their own line, after those of the meshes, with no conversion.
void testTexelsLine() {
    terrain::unreal::HorizonTimings timings;
    timings.recordUpload(terrain::unreal::HorizonSection::Texels, 0.0, 0.25, 1000);
    timings.recordUpload(terrain::unreal::HorizonSection::Texels, 0.0, 0.75, 2000);
    timings.recordTick(-1, false, 0.01);
    const std::vector<std::string> lines = terrain::unreal::describe(timings.take());
    TEST_CHECK(lines.size() == 2);
    TEST_CHECK(lines.back() ==
               "horizon texels: upload 2 ticks 0.50 ms (max 0.75), 1500 texels per tick");
    TEST_CHECK(terrain::unreal::describe(timings.take()).empty());
    // The detail after the epochs has its own line, last.
    timings.recordDetail(30.0);
    timings.recordDetail(90.0);
    const std::vector<std::string> detail = terrain::unreal::describe(timings.take());
    TEST_CHECK(detail.size() == 1 &&
               detail.front() == "horizon detail: 2 epochs 60.00 ms (max 90.00) from commit to "
                                 "crests and normals in");
}

} // namespace

int main() {
    testFirstAndSteps();
    testWorldTurn();
    testEdge();
    testGrids();
    testMorphFactor();
    testMorphTarget(terrain::HorizonParams{}.crestRatio);
    testMorphTarget(0);
    testTexelsLine();
    return test::result();
}
