// The crests of the GPU horizon computed after the epochs (HorizonGpuParams::deferCrests), around
// mountains: no level opens at its border while it, or the next coarser level, waits for its
// crests (every mix of raised and point textures, the vertex shader of the morph emulated with
// horizon_morph); with player edits (a fake source in the format of agent 26: the edited share s
// of a cell and the height E of the edited ground), the cells without edit (s = 0) rise as with the
// crests in the epochs, bit for bit, and the edited ones (0 < s <= 1) keep the height of their
// epoch, E where wholly edited, never lifted.
#include "horizon_gpu_streamer.hpp"
#include "horizon_morph.hpp"

#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <utility>

namespace {

using gpu_test::Gpu;
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

// The format of the edit summaries of agent 26: the share of the cell its edits cover and the
// height of the edited ground there.
struct ShareEdit {
    float share = 0.0f;
    float height = 0.0f;
};

// Wholly edited within 1.2 km of a point, half edited up to 2.4 km, nothing beyond.
std::optional<ShareEdit> editAt(double x, double z) {
    const double dx = x - (horizon_test::kX + 2'000.0);
    const double dz = z - (horizon_test::kZ - 1'500.0);
    const double squared = dx * dx + dz * dz;
    if (squared < 1'200.0 * 1'200.0) {
        return ShareEdit{1.0f, 1234.5f};
    }
    if (squared < 2'400.0 * 2'400.0) {
        return ShareEdit{0.5f, 1234.5f};
    }
    return std::nullopt;
}

// The edit as the EditSummary of this version applies it (the height changes by the edited share
// of the way from the ground to E). To adapt when the format of agent 26 is merged.
terrain::EditSummarySource editSource(const terrain::Heightfield& field) {
    return [&field](const terrain::CellFootprint& cell) -> std::optional<terrain::EditSummary> {
        const std::optional<ShareEdit> edit = editAt(cell.centerX, cell.centerZ);
        if (!edit) {
            return std::nullopt;
        }
        const float ground = field.heightAt(cell.centerX, cell.centerZ);
        return terrain::EditSummary{edit->share * (edit->height - ground), std::nullopt};
    };
}

struct EditCounts {
    int plainRaised = 0;  // cells without edit, raised as in the epochs
    int keptEdited = 0;   // edited cells the epochs would have raised, kept by the deferred crests
    int whole = 0;        // wholly edited cells, at E
    int partial = 0;      // partly edited cells
};

void testEdits(const horizon_test::World& world) {
    const auto inPlace = [](std::function<void()> job) { job(); };
    HorizonGpuStreamer deferred(world.field, world.ecology, kWorld, paramsOf(2, true), inPlace,
                                editSource(world.field));
    HorizonGpuStreamer epochs(world.field, world.ecology, kWorld, paramsOf(0, false), inPlace,
                              editSource(world.field));
    Gpu after(129);
    Gpu before(129);
    after.apply(deferred.update({}, kFrame));
    before.apply(epochs.update({}, kFrame));
    TEST_CHECK(!deferred.busy() && !epochs.busy());
    EditCounts counts;
    bool rule = true;
    for (int n = 0; n < kLevels; ++n) {
        const terrain::HorizonLevel& level = deferred.clipmap().level(n);
        for (int k = 0; k <= kCells; ++k) {
            for (int i = 0; i <= kCells; ++i) {
                const terrain::HorizonCell& cell = level.atWindow(i, k);
                const HorizonTexel shown = after.texel(n, i, k);
                if ((cell.flags & terrain::kHorizonEdited) == 0) {
                    rule = rule && shown == before.texel(n, i, k);
                    counts.plainRaised += shown.height > cell.height ? 1 : 0;
                    continue;
                }
                rule = rule && shown == terrain::unreal::horizonTexel(cell, true);
                counts.keptEdited += shown == before.texel(n, i, k) ? 0 : 1;
                const terrain::HorizonIndex c = terrain::canonicalSample(
                    {level.window().x0 + i, level.window().z0 + k}, level.counts());
                const std::optional<ShareEdit> edit =
                    editAt(terrain::samplePosition(c.x, level.pitch()),
                           terrain::samplePosition(c.z, level.pitch()));
                if (edit && edit->share == 1.0f) {
                    ++counts.whole;
                    rule = rule && std::abs(shown.height - edit->height) < 1e-2f;
                } else {
                    ++counts.partial;
                }
            }
        }
    }
    std::printf("edits: %d cells without edit raised, %d edited cells kept without their crest, "
                "%d wholly and %d partly edited\n",
                counts.plainRaised, counts.keptEdited, counts.whole, counts.partial);
    TEST_CHECK(rule);
    TEST_CHECK(counts.plainRaised > 0 && counts.keptEdited > 0 && counts.whole > 0 &&
               counts.partial > 0);
}

} // namespace

int main() {
    try {
        const horizon_test::World world(test_relief::legacyRelief());
        testSeams(world);
        testEdits(world);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        TEST_CHECK(false);
    }
    return test::result();
}
