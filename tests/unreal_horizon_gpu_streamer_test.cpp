// GPU horizon of the Unreal plugin (Bridge/horizon_gpu_streamer, Bridge/horizon_morph), on real
// levels with small windows: the first epoch writes every texel and places every level, a step
// only writes the bands of the levels that moved, the simulated textures and placements after a
// walk (across the edge of the world too) equal those of a streamer that went straight there, a
// world turn only moves the levels, the grids, and the geomorphing: its factor (0 inside, 1 on
// the border wherever the viewer stands, continuous) and its target, the surface of the coarser
// level at every sample of the finer one.
#include "horizon_gpu_streamer.hpp"
#include "horizon_morph.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace {

using terrain::HorizonIndex;
using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::HorizonTexel;
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

// What the GPU of Unreal holds: a texture and a placement per level.
class Gpu {
public:
    explicit Gpu(int side) : side_(side) {}

    void apply(const HorizonGpuUpdate& update) {
        for (const terrain::unreal::TexelRegion& region : update.regions) {
            std::vector<float>& texels = textures_[region.level];
            texels.resize(static_cast<std::size_t>(side_ * side_ * kTexelFloats), -9.0f);
            for (int row = 0; row < region.height; ++row) {
                for (int column = 0; column < region.width * kTexelFloats; ++column) {
                    texels[static_cast<std::size_t>(((region.z + row) * side_ + region.x) *
                                                        kTexelFloats +
                                                    column)] =
                        region.data[static_cast<std::size_t>(row * region.width * kTexelFloats +
                                                             column)];
                }
            }
        }
        for (const LevelPlacement& placement : update.placements) {
            placements_[placement.level] = placement;
        }
    }

    // The texel the material reads for the sample (i, k) of the window of a level.
    [[nodiscard]] HorizonTexel texel(int level, int i, int k) const {
        const LevelPlacement& placement = placements_.at(level);
        const auto x = static_cast<int>((placement.firstSlot.x + i) % side_);
        const auto z = static_cast<int>((placement.firstSlot.z + k) % side_);
        const std::vector<float>& texels = textures_.at(level);
        const auto first = static_cast<std::size_t>((z * side_ + x) * kTexelFloats);
        return {texels[first], texels[first + 1], texels[first + 2], texels[first + 3]};
    }

    // The same windows read by the material, the same origins and trims.
    [[nodiscard]] bool sameView(const Gpu& other) const {
        for (const auto& [level, placement] : placements_) {
            const LevelPlacement& theirs = other.placements_.at(level);
            if (!(placement.origin == theirs.origin) || placement.trim != theirs.trim ||
                !(placement.inCoarser == theirs.inCoarser)) {
                return false;
            }
            for (int k = 0; k < side_; ++k) {
                for (int i = 0; i < side_; ++i) {
                    if (!(texel(level, i, k) == other.texel(level, i, k))) {
                        return false;
                    }
                }
            }
        }
        return placements_.size() == other.placements_.size();
    }

    [[nodiscard]] const std::map<int, LevelPlacement>& placements() const { return placements_; }

private:
    int side_;
    std::map<int, std::vector<float>> textures_;
    std::map<int, LevelPlacement> placements_;
};

struct Session {
    Session()
        : streamer(world.field, world.ecology, kWorld, smallParams(),
                   [](std::function<void()> job) { job(); }) {}

    horizon_test::World world;
    HorizonGpuStreamer streamer;
    Gpu gpu{129};
};

// The texels of every window match the samples of the clipmap.
bool texelsMatch(const Session& session) {
    const terrain::HorizonClipmap& clipmap = session.streamer.clipmap();
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        for (int k = 0; k < 129; ++k) {
            for (int i = 0; i < 129; ++i) {
                if (!(session.gpu.texel(n, i, k) ==
                      terrain::unreal::horizonTexel(clipmap.level(n).atWindow(i, k), true))) {
                    return false;
                }
            }
        }
    }
    return true;
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
    TEST_CHECK(texels == 5u * 129u * 129u);
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
    const double u = placement.inCoarser.x + i / 2.0;
    const double v = placement.inCoarser.z + k / 2.0;
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

// The morph target of every sample of every finer level lies on the coarser surface: the even
// samples bit for bit, the others within the rounding of a float mean.
void testMorphTarget() {
    Session session;
    session.gpu.apply(session.streamer.update({0.0, 0.0, 0.0}, kFrame));
    bool exact = true;
    double worst = 0.0;
    for (int fine = 0; fine + 1 < 5; ++fine) {
        for (int k = 0; k <= 128; ++k) {
            for (int i = 0; i <= 128; ++i) {
                const terrain::unreal::MorphSources from = terrain::unreal::morphSources(i, k);
                const float target = 0.5f * (session.gpu.texel(fine, from.i0, from.k0).height +
                                             session.gpu.texel(fine, from.i1, from.k1).height);
                const double expected = coarseSurface(session, fine, i, k);
                if (i % 2 == 0 && k % 2 == 0) {
                    exact = exact && static_cast<double>(target) == expected;
                } else {
                    worst = std::max(worst, std::abs(target - expected));
                }
            }
        }
    }
    TEST_CHECK(exact);
    TEST_CHECK(worst < 1e-3);
    TEST_CHECK((terrain::unreal::morphSources(3, 5) == terrain::unreal::MorphSources{4, 4, 2, 6}));
    TEST_CHECK((terrain::unreal::morphSources(3, 4) == terrain::unreal::MorphSources{2, 4, 4, 4}));
    TEST_CHECK((terrain::unreal::morphSources(2, 5) == terrain::unreal::MorphSources{2, 4, 2, 6}));
}

} // namespace

int main() {
    testFirstAndSteps();
    testWorldTurn();
    testEdge();
    testGrids();
    testMorphFactor();
    testMorphTarget();
    return test::result();
}
