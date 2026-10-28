// The detail normals around razed boxes on the GPU path (Bridge/horizon_gpu_streamer): the
// texels of the samples edited over half of their footprint hold the slopes of the grid, never
// pending. The material filters the texels bilinearly (weights in 1/256) and takes a slope beyond
// kNormalPendingTest for pending: a pending texel mixed with real ones by a small weight gave a
// slope below it, a normal lying flat lit by the sky alone, a line along the corners of a razed
// box seen from 11 km. Here every filtered slope around the corners is real; the texels of the
// edited samples are the smooth slopes of the grid; and a second edit against a box gives the
// textures of a horizon started after both.
#include "horizon_gpu_streamer.hpp"
#include "terrain_edits.hpp"
#include "terrain_session.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"
#include "unreal_texture_mirror.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>

namespace {

using terrain::unreal::floatFromHalf;
using terrain::unreal::GroundEdit;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::kNormalPendingTest;
using terrain::unreal::kNormalSlopeLimit;
using terrain::unreal::TerrainSession;

constexpr double kX = streaming_test::kCenterX + 8.0;
constexpr double kZ = streaming_test::kCenterZ + 8.0;
constexpr int kSteps = 64; // filtered positions per texel along each axis

terrain::unreal::SessionParams sessionParams() {
    terrain::unreal::SessionParams params;
    params.stream.lod = {{1, 3, 7}, 23};
    params.stream.workerThreads = 0;
    return params;
}

terrain::unreal::HorizonGpuParams gpuParams() {
    terrain::unreal::HorizonGpuParams params;
    params.levels = 5;
    params.cellsPerSide = 128;
    params.lod = {{1, 3, 7}, 23};
    return params;
}

// A GPU horizon around the player and the textures it wrote, its jobs run at once.
struct Horizon {
    HorizonGpuStreamer streamer;
    terrain::unreal::UnrealFrame frame;
    horizon_test::TextureMirror textures;

    explicit Horizon(TerrainSession& session)
        : streamer(
              session.tiles().heightfield(), session.tiles().ecology(), session.tiles().world(),
              gpuParams(), [](std::function<void()> job) { job(); }, &session.horizonEdits()),
          frame(session.frameAt(kX, kZ)) {
        settle();
    }

    // Updates until every sample and detail texel is written.
    void settle() {
        int quiet = 0;
        for (int n = 0; n < 200 && quiet < 3; ++n) {
            const terrain::unreal::HorizonGpuUpdate update = streamer.update({}, frame);
            textures.apply(update);
            quiet = update.empty() && !update.committed ? quiet + 1 : 0;
        }
    }
};

// The filtered slopes around a corner (x, z) of a box on a level, as the material reads them:
// the real ones (at most kNormalSlopeLimit), the pending ones, and the broken ones, mixed with a
// pending texel and taken for real.
struct Filtered {
    std::int64_t real = 0;
    std::int64_t pending = 0;
    std::int64_t broken = 0;
};

Filtered filterAround(const Horizon& horizon, int levelIndex, double x, double z) {
    const terrain::HorizonLevel& level = horizon.streamer.clipmap().level(levelIndex);
    const int ratio = horizon.textures.ratio;
    // The texel coordinates of the cell of the corner (material: f + first * ratio).
    const auto texelOf = [&](double meters, std::int64_t windowLow, std::int64_t first) {
        const auto cell = static_cast<std::int64_t>(std::floor(meters / level.pitch()));
        return static_cast<int>((cell - windowLow + first) * ratio);
    };
    const int tx0 = texelOf(x, level.window().x0, level.firstSlot().x) - ratio;
    const int tz0 = texelOf(z, level.window().z0, level.firstSlot().z) - ratio;
    Filtered filtered;
    for (int sz = 0; sz < 2 * ratio * kSteps; ++sz) {
        for (int sx = 0; sx < 2 * ratio * kSteps; ++sx) {
            // The weights of the hardware, in 1/256 (kSteps divides 256).
            const double u = (sx % kSteps) / static_cast<double>(kSteps);
            const double v = (sz % kSteps) / static_cast<double>(kSteps);
            const int tx = tx0 + sx / kSteps;
            const int tz = tz0 + sz / kSteps;
            const std::array<double, 4> weights = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v,
                                                   u * v};
            std::array<float, 2> g{};
            bool mixesPending = false;
            for (std::size_t c = 0; c < weights.size(); ++c) {
                const std::uint16_t* texel = horizon.textures.normalAt(
                    levelIndex, tx + static_cast<int>(c % 2), tz + static_cast<int>(c / 2));
                const float gx = floatFromHalf(texel[0]);
                const float gz = floatFromHalf(texel[1]);
                mixesPending =
                    mixesPending ||
                    (weights[c] > 0.0 && std::max(std::abs(gx), std::abs(gz)) > kNormalPendingTest);
                g[0] += static_cast<float>(weights[c]) * gx;
                g[1] += static_cast<float>(weights[c]) * gz;
            }
            const float slope = std::max(std::abs(g[0]), std::abs(g[1]));
            if (slope > kNormalPendingTest) {
                ++filtered.pending;
            } else if (mixesPending) {
                ++filtered.broken;
            } else {
                filtered.real += slope <= kNormalSlopeLimit ? 1 : 0;
            }
        }
    }
    return filtered;
}

// Whether the window of a level holds the cell of (x, z) and its neighbors.
bool holds(const terrain::HorizonLevel& level, double x, double z) {
    const auto i = static_cast<std::int64_t>(std::floor(x / level.pitch())) - level.window().x0;
    const auto k = static_cast<std::int64_t>(std::floor(z / level.pitch())) - level.window().z0;
    return i >= 2 && k >= 2 && i < level.side() - 3 && k < level.side() - 3;
}

// Every filtered slope around the 4 corners of a box is real, on every level that holds them.
void checkCorners(const Horizon& horizon, const terrain::VolumeEdit& box, const char* name) {
    const double x0 = box.anchor.x;
    const double z0 = box.anchor.z;
    const double x1 = x0 + box.size[0];
    const double z1 = z0 + box.size[2];
    Filtered all;
    int levels = 0;
    for (int n = 0; n < horizon.streamer.clipmap().levelCount(); ++n) {
        const terrain::HorizonLevel& level = horizon.streamer.clipmap().level(n);
        if (!holds(level, x0, z0) || !holds(level, x1, z1)) {
            continue;
        }
        ++levels;
        for (const auto& [x, z] :
             {std::pair{x0, z0}, std::pair{x1, z0}, std::pair{x0, z1}, std::pair{x1, z1}}) {
            const Filtered corner = filterAround(horizon, n, x, z);
            all.real += corner.real;
            all.pending += corner.pending;
            all.broken += corner.broken;
        }
    }
    const std::int64_t perCorner = 2 * horizon.textures.ratio * kSteps;
    const std::int64_t positions = static_cast<std::int64_t>(levels) * 4 * perCorner * perCorner;
    std::printf("%s: corners on %d levels, %lld filtered slopes: %lld real, %lld pending, %lld "
                "broken\n",
                name, levels, static_cast<long long>(positions), static_cast<long long>(all.real),
                static_cast<long long>(all.pending), static_cast<long long>(all.broken));
    TEST_CHECK(levels >= 2 && all.real == positions && all.pending == 0 && all.broken == 0);
}

// The texels of the samples of a level edited over half of their footprint: the slopes of the
// grid, bilinear between the texels of the samples of their cell.
void checkGridSlopes(const Horizon& horizon, TerrainSession& session, int levelIndex) {
    const terrain::HorizonLevel& level = horizon.streamer.clipmap().level(levelIndex);
    const int ratio = horizon.textures.ratio;
    const int side = level.side();
    std::int64_t samples = 0;
    bool match = true;
    for (int k = 0; k + 1 < side; ++k) {
        for (int i = 0; i + 1 < side; ++i) {
            if ((level.atWindow(i, k).flags & terrain::kHorizonEdited) == 0) {
                continue;
            }
            const terrain::FootprintEdits edits = session.horizonEdits().footprint(
                {levelIndex, terrain::samplePosition(level.window().x0 + i, level.pitch()),
                 terrain::samplePosition(level.window().z0 + k, level.pitch()),
                 level.pitch() / 2.0});
            if (2 * edits.edited < edits.columns) {
                continue;
            }
            ++samples;
            std::array<terrain::unreal::HorizonTexel, 4> corners;
            for (std::size_t c = 0; c < corners.size(); ++c) {
                corners[c] = terrain::unreal::horizonTexel(
                    level.atWindow(i + static_cast<int>(c % 2), k + static_cast<int>(c / 2)), true);
            }
            for (int b = 0; b < ratio; ++b) {
                for (int a = 0; a < ratio; ++a) {
                    const double u = static_cast<double>(a) / ratio;
                    const double v = static_cast<double>(b) / ratio;
                    const std::array<double, 4> w = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v,
                                                     u * v};
                    double gx = 0.0;
                    double gz = 0.0;
                    for (std::size_t c = 0; c < corners.size(); ++c) {
                        gx += w[c] * corners[c].gradX;
                        gz += w[c] * corners[c].gradZ;
                    }
                    const std::uint16_t* texel = horizon.textures.normalAt(
                        levelIndex, static_cast<int>((level.firstSlot().x + i) % side) * ratio + a,
                        static_cast<int>((level.firstSlot().z + k) % side) * ratio + b);
                    match = match &&
                            texel[0] == terrain::unreal::halfFromFloat(static_cast<float>(gx)) &&
                            texel[1] == terrain::unreal::halfFromFloat(static_cast<float>(gz));
                }
            }
        }
    }
    std::printf("level %d: %lld samples follow the grid\n", levelIndex,
                static_cast<long long>(samples));
    TEST_CHECK(samples > 10 && match);
}

void testRazedBoxes() {
    TerrainSession session(sessionParams());
    Horizon horizon(session);
    const terrain::Heightfield& field = session.tiles().heightfield();
    // A square of 160 m 300 m away (level 0, under the tiles and beyond), one of 600 m 3 km away
    // (levels 3 and 4), razed 40 m below their lowest ground.
    const terrain::VolumeEdit nearBox =
        terrain::unreal::groundEditAt(field, {GroundEdit::Raze, 0.0, 160.0, 40.0}, kX, kZ - 300.0);
    const terrain::VolumeEdit farBox = terrain::unreal::groundEditAt(
        field, {GroundEdit::Raze, 0.0, 600.0, 40.0}, kX + 3000.0, kZ + 500.0);
    session.applyEdit(nearBox);
    session.applyEdit(farBox);
    horizon.settle();
    checkCorners(horizon, nearBox, "razed square of 160 m 300 m away");
    checkCorners(horizon, farBox, "razed square of 600 m 3 km away");
    checkGridSlopes(horizon, session, 0);
    checkGridSlopes(horizon, session, 3);

    // An explosion against the east wall of the far box: its samples, and the texels of their
    // neighbors that interpolate toward them, as a horizon started after both edits.
    session.applyEdit(terrain::unreal::groundEditAt(field, {GroundEdit::Blast, 0.0, 60.0, 10.0},
                                                    farBox.anchor.x + farBox.size[0],
                                                    farBox.anchor.z + farBox.size[2] / 2.0));
    horizon.settle();
    checkCorners(horizon, farBox, "the same with an explosion against its wall");
    const Horizon fresh(session);
    TEST_CHECK(horizon.textures.texels == fresh.textures.texels);
    TEST_CHECK(horizon.textures.normals == fresh.textures.normals);
}

} // namespace

int main() {
    testRazedBoxes();
    return test::result();
}
