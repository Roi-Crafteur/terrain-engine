// The surface of level 0 the materials of the tiles read after a teleport
// (Bridge/horizon_surface.hpp, UTerrainHorizonClipmap::PlaceSurface): the GPU horizon brought to
// the new place level by level (HorizonGpuParams::progressive), its level 0 late there (the pieces
// of the old place still in flight, then its own epoch), its jobs run one at a time:
//   - while level 0 is hidden, no surface: no point the tiles cover there leaves its water to the
//     horizon (horizonWaterShare 0), which draws none there; the rule of 52e23c9 (the window of
//     the old place kept while level 0 is hidden, the share without the window) leaves it all;
//   - once level 0 is shown there, its window: the tiles keep their water around the player and
//     leave it to the horizon across the outer band, as before the teleport;
//   - the share itself: none without a surface or outside the window, 0 inside the band, 1 at
//     its outer edge (the reference of the material, water_look_hlsl.py).
#include "horizon_surface.hpp"
#include "horizon_test_support.hpp"
#include "terrain_surface.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>

namespace {

using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::LevelPlacement;
using terrain::unreal::SurfaceWindow;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;
const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};
constexpr double kStepMeters = 16.0; // the points checked around the player

HorizonGpuParams params() {
    HorizonGpuParams result;
    result.levels = 5;
    result.cellsPerSide = 128;
    result.lod = {{1, 3, 7}, 23};
    result.normalRatio = 0;
    result.crestRatio = 0;
    result.progressive = true;
    return result;
}

UnrealPoint at(double x, double z) {
    return terrain::unreal::toUnreal(kFrame, {x, 0.0, z});
}

// The streamer, its jobs run when the test says so, and the surface the tiles read: by the rule
// of the plugin (surfaceAfter), and by that of 52e23c9 (the window of level 0 set when it is
// shown, never cleared).
struct Horizon {
    horizon_test::World world;
    gpu_test::Jobs jobs;
    HorizonGpuStreamer streamer{world.field, world.ecology, kWorld, params(), jobs.launcher()};
    std::optional<SurfaceWindow> surface;
    std::optional<SurfaceWindow> stale;

    void update(double x, double z) {
        const HorizonGpuUpdate result = streamer.update(at(x, z), kFrame);
        for (const LevelPlacement& placement : result.placements) {
            surface = terrain::unreal::surfaceAfter(surface, placement);
            if (placement.level == 0 && placement.visible) {
                stale = SurfaceWindow{placement.origin, placement.firstSlot};
            }
        }
    }
    void settle(double x, double z) {
        for (int round = 0; round < 200; ++round) {
            update(x, z);
            if (!streamer.busy()) {
                return;
            }
            jobs.runAll();
        }
    }
};

// What the materials of the tiles get (UTerrainHorizonClipmap::BindSurfaceLook).
struct Look {
    double cells = 0.0;
    double pitchCm = 0.0;
    double coveredCm = 0.0;
    double bandCm = 0.0;

    explicit Look(const HorizonGpuStreamer& streamer) {
        const double pitch = terrain::levelPitch(streamer.params(), 0);
        const terrain::unreal::DropFade fade =
            terrain::unreal::dropFade(streamer.tileHole(), pitch);
        cells = streamer.side() - 1;
        pitchCm = pitch * 100.0;
        coveredCm = fade.coveredMeters * 100.0;
        bandCm = 2.0 * fade.fadeMeters * 100.0;
    }
    [[nodiscard]] double share(const std::optional<SurfaceWindow>& s, const UnrealPoint& p) const {
        return terrain::unreal::horizonWaterShare(s.has_value(), s ? p.x - s->origin.x : 0.0,
                                                  s ? p.y - s->origin.y : 0.0, cells, pitchCm,
                                                  coveredCm, bandCm);
    }
    // The share of 52e23c9: FTerrainWater::HorizonShare under HasSurface alone.
    [[nodiscard]] double staleShare(const std::optional<SurfaceWindow>& s,
                                    const UnrealPoint& p) const {
        if (!s) {
            return 0.0;
        }
        const double i = (p.x - s->origin.x) / pitchCm - 0.5 * cells;
        const double j = (p.y - s->origin.y) / pitchCm - 0.5 * cells;
        const double fade = std::max(0.5 * bandCm, 1.0);
        const double from = std::max(std::abs(i), std::abs(j)) * pitchCm;
        return std::clamp((from - (coveredCm - fade)) / fade, 0.0, 1.0);
    }
};

struct Counts {
    std::size_t points = 0; // the points the tiles cover around the player
    std::size_t lost = 0;   // of them left to the horizon (a share above 0)
    std::size_t staleLost = 0;
};

// The points within reach meters of (x, z) along x and z, every kStepMeters.
Counts around(const Horizon& h, const Look& look, double x, double z, double reach) {
    Counts c;
    for (double dz = -reach; dz <= reach; dz += kStepMeters) {
        for (double dx = -reach; dx <= reach; dx += kStepMeters) {
            const UnrealPoint p = at(x + dx, z + dz);
            ++c.points;
            c.lost += look.share(h.surface, p) > 0.0 ? 1u : 0u;
            c.staleLost += look.staleShare(h.stale, p) > 0.0 ? 1u : 0u;
        }
    }
    return c;
}

// The share: none without a surface or outside the window, 0 inside the band, 1 at its edge.
void testShare() {
    using terrain::unreal::horizonWaterShare;
    const double cells = 128.0;
    const double pitch = 800.0;      // cm
    const double center = 64.0 * pitch;
    const double covered = 40'000.0; // cm from the center
    const double band = 16'000.0;    // its inner half kept by the tiles
    const auto share = [&](bool placed, double x, double y) {
        return horizonWaterShare(placed, x, y, cells, pitch, covered, band);
    };
    TEST_CHECK(share(false, center + covered, center) == 0.0);
    TEST_CHECK(share(true, center, center) == 0.0);
    TEST_CHECK(share(true, center + 32'000.0, center) == 0.0);
    TEST_CHECK(share(true, center + 36'000.0, center) == 0.5);
    TEST_CHECK(share(true, center, center + covered) == 1.0);
    TEST_CHECK(share(true, -1.0, center) == 0.0);                 // outside the window
    TEST_CHECK(share(true, center, cells * pitch + 1.0) == 0.0);
}

// Placements of other levels leave the surface; level 0 hidden clears it, shown sets it.
void testAfter() {
    const SurfaceWindow window{{100.0, 200.0, 300.0}, {4, 5}};
    LevelPlacement coarse;
    coarse.level = 2;
    coarse.visible = false;
    TEST_CHECK(terrain::unreal::surfaceAfter(window, coarse) == window);
    LevelPlacement hidden;
    hidden.visible = false;
    TEST_CHECK(!terrain::unreal::surfaceAfter(window, hidden).has_value());
    LevelPlacement shown;
    shown.origin = {7.0, 8.0, 9.0};
    shown.firstSlot = {1, 2};
    TEST_CHECK(terrain::unreal::surfaceAfter(std::nullopt, shown) ==
               SurfaceWindow({{7.0, 8.0, 9.0}, {1, 2}}));
}

// A teleport of `jump` meters in the middle of an epoch, level 0 late at the new place.
void testTeleport(double jump) {
    Horizon h;
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    h.settle(x, z);
    const Look look(h.streamer);
    const double reach = look.coveredCm / 100.0;
    const double inside = reach - look.bandCm / 100.0;
    const Counts before = around(h, look, x, z, inside);
    TEST_CHECK(h.surface.has_value() && before.lost == 0);
    TEST_CHECK(around(h, look, x, z, reach).lost > 0); // the band left to the horizon
    h.update(x + 300.0, z); // a step: an epoch in flight, its pieces queued
    TEST_CHECK(h.streamer.busy());
    const double far = x + jump;
    std::size_t late = 0;
    Counts during;
    for (int round = 0; round < 500; ++round) {
        h.update(far, z);
        if (h.surface.has_value()) {
            break; // level 0 shown there
        }
        ++late;
        const Counts c = around(h, look, far, z, reach);
        during.points += c.points;
        during.lost += c.lost;
        during.staleLost += c.staleLost;
        if (h.jobs.queue.empty()) {
            break;
        }
        std::function<void()> job = std::move(h.jobs.queue.front());
        h.jobs.queue.pop_front();
        job(); // one job at a time: level 0 late
    }
    h.settle(far, z);
    const Counts after = around(h, look, far, z, inside);
    const Counts band = around(h, look, far, z, reach);
    std::printf("teleport of %.0f m: %zu updates with level 0 hidden there, %zu points the tiles "
                "cover checked, %zu of them left to the horizon (%zu by the rule of 52e23c9); once "
                "shown, %zu of the %zu points inside the band left to it, %zu of the %zu up to "
                "its edge\n",
                jump, late, during.points, during.lost, during.staleLost, after.lost, after.points,
                band.lost, band.points);
    TEST_CHECK(late >= 2 && during.points > 0);
    TEST_CHECK(during.lost == 0);
    TEST_CHECK(during.staleLost == during.points); // the scenario of rampe_milieu
    TEST_CHECK(h.surface.has_value() && after.lost == 0 && band.lost > 0);
}

} // namespace

int main() {
    testShare();
    testAfter();
    testTeleport(3'000.0);
    testTeleport(300'000.0);
    return test::result();
}
