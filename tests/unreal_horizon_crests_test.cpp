// The crests of the GPU horizon computed after the epochs (HorizonGpuParams::deferCrests), around
// mountains: no level opens at its border while it, or the next coarser level, waits for its
// crests (every mix of raised and point textures, the vertex shader of the morph emulated with
// horizon_morph); with player edits (the HorizonEditMap of the player edits: a mountain razed to a
// floor and a crater), the cells without edit rise as with the crests in the epochs, bit for bit,
// and the edited ones keep the height of their epoch, never lifted: the same as in the epochs on
// the razed floor, whose summaries bound the ground to the floor.
#include "horizon_gpu_streamer.hpp"
#include "horizon_morph.hpp"

#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

using gpu_test::Gpu;
using terrain::EditMode;
using terrain::EditShape;
using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonTexel;
using terrain::unreal::LevelPlacement;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;
const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};
constexpr int kCells = 128;
constexpr int kLevels = 4;

HorizonGpuParams paramsOf(int normalRatio, bool deferred) {
    HorizonGpuParams params;
    params.levels = kLevels;
    params.cellsPerSide = kCells;
    params.lod = {{1, 3, 7}, 23};
    params.normalRatio = normalRatio;
    params.crestRatio = 2;
    params.deferCrests = deferred;
    return params;
}

// The height of the coarser level under the vertex (i, k) of the finer one: its triangles.
double coarseSurface(const Gpu& gpu, int fine, int i, int k) {
    const LevelPlacement& placement = gpu.placements().at(fine);
    const double u = static_cast<double>(placement.inCoarser.x) + i / 2.0;
    const double v = static_cast<double>(placement.inCoarser.z) + k / 2.0;
    const int ci = static_cast<int>(std::floor(u));
    const int ck = static_cast<int>(std::floor(v));
    const double s = u - ci;
    const double t = v - ck;
    const auto h = [&](int a, int b) {
        return static_cast<double>(gpu.texel(fine + 1, a, b).height);
    };
    if (s + t <= 1.0) {
        return h(ci, ck) + s * (h(ci + 1, ck) - h(ci, ck)) + t * (h(ci, ck + 1) - h(ci, ck));
    }
    return h(ci + 1, ck + 1) + (1.0 - s) * (h(ci, ck + 1) - h(ci + 1, ck + 1)) +
           (1.0 - t) * (h(ci + 1, ck) - h(ci + 1, ck + 1));
}

struct Seam {
    double gap = 0.0; // the largest distance of a border vertex to the coarser surface, meters
    int moving = 0;   // border vertices whose own texel differs from their morph target
    bool morphed = true;
};

// The border vertices of a finer level as the vertex shader places them (morph factor 1 there, the
// mean of the two coarser texels of its edge), against the surface of the coarser level.
void measureSeam(const Gpu& gpu, int fine, Seam& seam) {
    const LevelPlacement& placement = gpu.placements().at(fine);
    const double pitch = 100.0 * 8.0 * (1 << fine); // centimeters
    const double u = (0.0 - placement.origin.x) / pitch;
    const double v = (0.0 - placement.origin.y) / pitch;
    for (int k = 0; k <= kCells; ++k) {
        for (int i = 0; i <= kCells; ++i) {
            if (i != 0 && i != kCells && k != 0 && k != kCells) {
                continue;
            }
            const double alpha = terrain::unreal::morphFactor(i, k, u, v, kCells, 32.0);
            const terrain::unreal::MorphSources from = terrain::unreal::morphSources(i, k);
            const auto coarse = [&](int a, int b) {
                return gpu.texel(fine + 1, static_cast<int>(placement.inCoarser.x) + a / 2,
                                 static_cast<int>(placement.inCoarser.z) + b / 2)
                    .height;
            };
            const float target = 0.5f * (coarse(from.i0, from.k0) + coarse(from.i1, from.k1));
            const float own = gpu.texel(fine, i, k).height;
            const float shown = own + static_cast<float>(alpha) * (target - own); // lerp
            seam.morphed = seam.morphed && alpha == 1.0;
            seam.moving += own != target ? 1 : 0;
            seam.gap = std::max(seam.gap, std::abs(shown - coarseSurface(gpu, fine, i, k)));
        }
    }
}

void testSeams(const horizon_test::World& world) {
    gpu_test::Jobs jobs;
    HorizonGpuStreamer streamer(world.field, world.ecology, kWorld, paramsOf(2, true),
                                jobs.launcher());
    Gpu points(129);
    points.apply(streamer.update({}, kFrame));
    while (!streamer.shown()) { // the pieces only: the detail of the commit stays queued
        jobs.runAll();
        points.apply(streamer.update({}, kFrame));
    }
    Gpu raised = points;
    while (streamer.busy()) {
        jobs.runAll();
        raised.apply(streamer.update({}, kFrame));
    }
    Seam seam;
    for (int mask = 0; mask < (1 << kLevels); ++mask) {
        Gpu mixed = points;
        for (int n = 0; n < kLevels; ++n) {
            if ((mask & (1 << n)) != 0) {
                mixed.texture(n) = raised.texture(n);
            }
        }
        for (int fine = 0; fine + 1 < kLevels; ++fine) {
            measureSeam(mixed, fine, seam);
        }
    }
    std::printf("seams, every mix of raised and point levels: largest gap %.6f m, %d border "
                "vertices moved by the morph\n",
                seam.gap, seam.moving);
    TEST_CHECK(seam.morphed && seam.moving > 0 && seam.gap < 1e-2);
    TEST_CHECK(!points.sameView(raised));
}

// A mountain of the default world razed to a flat floor over 1 km (the ground of the edited
// columns there is the floor: the summaries of its wholly edited footprints have the range
// [floor, floor]) and a crater of 40 m beside it, as the player makes them.
struct EditedWorld {
    terrain::TerrainTiles tiles{streaming_test::kSeed};
    terrain::HorizonEditMap map{tiles.heightfield()};
    double centerX = 0.0;
    double centerZ = 0.0;

    EditedWorld() {
        const terrain::Heightfield& field = tiles.heightfield();
        const terrain::ChunkCoord mountain = streaming_test::mountainChunk(field);
        const int x0 = mountain.x * 16 + 5;
        const int z0 = mountain.z * 16 + 7;
        float top = field.heightAt(x0, z0);
        for (int dz = 0; dz <= 999; dz += 25) {
            for (int dx = 0; dx <= 999; dx += 25) {
                top = std::max(top, field.heightAt(x0 + dx, z0 + dz));
            }
        }
        const int floor = static_cast<int>(std::floor(top)) - 499;
        edit({EditShape::Box, EditMode::Remove, {x0, floor, z0}, {1000, 500, 1000}, 0});
        const int cx = x0 + 1800;
        const int cz = z0 + 600;
        const int ground = static_cast<int>(std::floor(field.heightAt(cx, cz)));
        edit({EditShape::Sphere, EditMode::Remove, {cx, ground, cz}, {}, 40 * 16});
        TEST_CHECK(!map.applyStaged().empty());
        centerX = x0 + 500.0;
        centerZ = z0 + 500.0;
    }

    void edit(const terrain::VolumeEdit& volume) { map.stage(tiles, tiles.applyEdit(volume)); }
};

struct EditCounts {
    int plainRaised = 0; // cells without edit, raised as in the epochs
    int keptEdited = 0;  // edited cells the epochs would show otherwise, kept by the deferred crests
    int floor = 0;       // wholly edited cells of the razed floor, as in the epochs
    int partial = 0;     // the other edited cells
};

// The edits within the footprint of the sample (i, k) of the window of a level.
terrain::FootprintEdits footprintOf(const terrain::HorizonEditMap& map,
                                    const terrain::HorizonLevel& level, int n, int i, int k) {
    const terrain::HorizonIndex c = terrain::canonicalSample(
        {level.window().x0 + i, level.window().z0 + k}, level.counts());
    return map.footprint({n, terrain::samplePosition(c.x, level.pitch()),
                          terrain::samplePosition(c.z, level.pitch()), level.pitch() / 2.0});
}

// The rule of the deferred crests with the edit map of the player edits: the cells without edit
// rise as in the epochs, bit for bit; the edited ones keep the height their epoch gave them, never
// lifted, the same as in the epochs where the range of their summary decides it alone (a floor).
void testEdits() {
    EditedWorld world;
    const UnrealFrame frame{{world.centerX, 0.0, world.centerZ}};
    const terrain::Heightfield& field = world.tiles.heightfield();
    const auto inPlace = [](std::function<void()> job) { job(); };
    HorizonGpuStreamer deferred(field, world.tiles.ecology(), world.tiles.world(),
                                paramsOf(2, true), inPlace, &world.map);
    HorizonGpuStreamer epochs(field, world.tiles.ecology(), world.tiles.world(), paramsOf(0, false),
                              inPlace, &world.map);
    Gpu after(129);
    Gpu before(129);
    after.apply(deferred.update({}, frame));
    before.apply(epochs.update({}, frame));
    TEST_CHECK(!deferred.busy() && !epochs.busy());
    EditCounts counts;
    bool rule = true;
    for (int n = 0; n < kLevels; ++n) {
        const terrain::HorizonLevel& level = deferred.clipmap().level(n);
        for (int k = 0; k <= kCells; ++k) {
            for (int i = 0; i <= kCells; ++i) {
                const terrain::HorizonCell& cell = level.atWindow(i, k);
                const HorizonTexel shown = after.texel(n, i, k);
                const bool same = shown == before.texel(n, i, k);
                if ((cell.flags & terrain::kHorizonEdited) == 0) {
                    rule = rule && same;
                    counts.plainRaised += shown.height > cell.height ? 1 : 0;
                    continue;
                }
                rule = rule && shown == terrain::unreal::horizonTexel(cell, true);
                counts.keptEdited += same ? 0 : 1;
                const terrain::FootprintEdits edits = footprintOf(world.map, level, n, i, k);
                if (edits.edited == edits.columns && edits.low == edits.high) {
                    ++counts.floor;
                    rule = rule && same;
                } else {
                    ++counts.partial;
                }
            }
        }
    }
    std::printf("edits: %d cells without edit raised, %d edited cells kept without their crest, "
                "%d of the razed floor and %d other edited cells\n",
                counts.plainRaised, counts.keptEdited, counts.floor, counts.partial);
    TEST_CHECK(rule);
    TEST_CHECK(counts.plainRaised > 0 && counts.keptEdited > 0 && counts.floor > 0 &&
               counts.partial > 0);
}

} // namespace

int main() {
    try {
        const horizon_test::World world(test_relief::legacyRelief());
        testSeams(world);
        testEdits();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        TEST_CHECK(false);
    }
    return test::result();
}
