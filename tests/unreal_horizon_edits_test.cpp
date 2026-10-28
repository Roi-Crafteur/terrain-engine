// Player edits on the Unreal side (Bridge/terrain_session, terrain_edits, horizon_epochs,
// horizon_gpu_streamer, horizon_streamer): the console command and the edits it places ahead of
// the camera; the session regenerates the tiles of an edit and stages it for the horizon; on the
// GPU path the next epoch sends only the texels around it, which then match the edited samples,
// and the detail normals of the samples edited over half of their footprint follow the grid; on the
// CPU path an edit far away rebuilds the bodies of the levels it writes, and of a finer level
// whose border is stitched to samples it wrote.
#include "horizon_gpu_streamer.hpp"
#include "horizon_streamer.hpp"
#include "terrain_edits.hpp"
#include "terrain_session.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"
#include "unreal_texture_mirror.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using terrain::EditMode;
using terrain::EditShape;
using terrain::unreal::GroundEdit;
using terrain::unreal::GroundEditCommand;
using terrain::unreal::TerrainSession;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

terrain::unreal::SessionParams sessionParams() {
    terrain::unreal::SessionParams params;
    params.stream.lod = {{1, 3, 7}, 23};
    params.stream.workerThreads = 0;
    return params;
}

// A place on land: the center of the default world of the streaming tests.
constexpr double kX = streaming_test::kCenterX + 8.0;
constexpr double kZ = streaming_test::kCenterZ + 8.0;

void testCommands() {
    std::string error;
    const std::vector<std::string> raze = {"raze", "5000", "1000", "40"};
    const std::optional<GroundEditCommand> razed = terrain::unreal::parseGroundEdit(raze, error);
    TEST_CHECK(razed && razed->kind == GroundEdit::Raze && razed->distance == 5000.0 &&
               razed->size == 1000.0 && razed->depth == 40.0);
    const std::vector<std::string> blast = {"blast", "800", "32"};
    const std::optional<GroundEditCommand> blasted = terrain::unreal::parseGroundEdit(blast, error);
    TEST_CHECK(blasted && blasted->kind == GroundEdit::Blast && blasted->depth == 10.0);
    for (const std::vector<std::string>& bad : {std::vector<std::string>{},
                                                {"dig", "10", "10"},
                                                {"blast", "10"},
                                                {"heap", "10", "-3"},
                                                {"blast", "10", "5000"},
                                                {"blast", "10", "5", "2"},
                                                {"raze", "x", "10"}}) {
        error.clear();
        TEST_CHECK(!terrain::unreal::parseGroundEdit(bad, error) && !error.empty());
    }

    const UnrealFrame frame{{kX, 100.0, kZ}};
    const terrain::unreal::CorePoint ahead =
        terrain::unreal::pointAhead(frame, {0.0, 0.0, 500.0}, 0.0, -3.0, 250.0);
    TEST_CHECK(ahead.x == kX && ahead.z == kZ - 250.0); // Unreal -Y is the core north

    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const float ground = field.heightAt(kX, kZ);
    const terrain::VolumeEdit square =
        terrain::unreal::groundEditAt(field, {GroundEdit::Raze, 0.0, 200.0, 30.0}, kX, kZ);
    TEST_CHECK(square.shape == EditShape::Box && square.mode == EditMode::Remove);
    TEST_CHECK(square.size[0] == 200 && square.size[2] == 200 && square.anchor.x == kX - 100.0);
    const double bottom = square.anchor.y;
    TEST_CHECK(bottom <= ground - 30.0 && bottom + square.size[1] > ground);
    const terrain::VolumeEdit heap =
        terrain::unreal::groundEditAt(field, {GroundEdit::Heap, 0.0, 30.0, 10.0}, kX, kZ);
    TEST_CHECK(heap.shape == EditShape::Sphere && heap.mode == EditMode::Add &&
               heap.radius == 30 * 16 &&
               heap.anchor.y == static_cast<int>(std::floor(ground - 10.0)));
}

// Every edit of the session regenerates its tiles and is staged for the horizon.
void testSession() {
    TerrainSession session(sessionParams());
    const terrain::BlockCoord block{static_cast<int>(kX), 300, static_cast<int>(kZ)};
    TEST_CHECK(!session.setBlock(block, terrain::BlockEdit::Removed).empty());
    TEST_CHECK(session.horizonEdits().hasStaged());
    session.horizonEdits().applyStaged();
    TEST_CHECK(!session.clearBlock(block).empty() && session.horizonEdits().hasStaged());
    session.horizonEdits().applyStaged();
    const terrain::VolumeEdit blast = terrain::unreal::groundEditAt(
        session.tiles().heightfield(), {GroundEdit::Blast, 0.0, 12.0, 10.0}, kX, kZ);
    TEST_CHECK(!session.applyEdit(blast).empty() && session.horizonEdits().hasStaged());
    session.horizonEdits().applyStaged();
    TEST_CHECK(!session.horizonEdits().empty());
    TEST_CHECK(!session.replaceModifications({}).empty());
    session.horizonEdits().applyStaged();
    TEST_CHECK(session.horizonEdits().empty()); // the edits gone, the horizon as generated
}

terrain::unreal::HorizonGpuParams gpuParams() {
    terrain::unreal::HorizonGpuParams params;
    params.levels = 5;
    params.cellsPerSide = 128;
    params.lod = {{1, 3, 7}, 23};
    return params;
}

// The slot along an axis of the sample i of a window from its low corner, first slot first.
int slotOf(std::int64_t first, int i, int side) {
    return static_cast<int>((first + i) % side);
}

void testGpuPath() {
    TerrainSession session(sessionParams());
    const UnrealFrame frame = session.frameAt(kX, kZ);
    terrain::unreal::HorizonGpuStreamer streamer(
        session.tiles().heightfield(), session.tiles().ecology(), session.tiles().world(),
        gpuParams(), [](std::function<void()> job) { job(); }, &session.horizonEdits());
    horizon_test::TextureMirror textures;
    textures.apply(streamer.update({0.0, 0.0, 0.0}, frame));
    // A square of 160 m razed 300 m north of the player, in the window of level 0.
    const terrain::VolumeEdit razed = terrain::unreal::groundEditAt(
        session.tiles().heightfield(), {GroundEdit::Raze, 0.0, 160.0, 10.0}, kX, kZ - 300.0);
    session.applyEdit(razed);
    const terrain::unreal::HorizonGpuUpdate update = streamer.update({0.0, 0.0, 0.0}, frame);
    textures.apply(update);
    std::int64_t texels = 0;
    for (const terrain::unreal::TexelRegion& region : update.regions) {
        texels += static_cast<std::int64_t>(region.width) * region.height;
    }
    std::printf(
        "razed square of 160 m 300 m away: %lld samples sent in %zu regions, %zu regions of detail "
        "normals\n",
        static_cast<long long>(update.samples), update.regions.size(), update.normalRegions.size());
    TEST_CHECK(update.committed && update.coarsestMoved == -1 && update.placements.empty());
    TEST_CHECK(texels == update.samples && update.samples > 100 && update.samples < 2'000);
    // Every texel the material reads is the sample of the clipmap; the middle of the square has
    // pending detail normals, the player's sample its detail.
    const terrain::HorizonClipmap& clipmap = streamer.clipmap();
    bool match = true;
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        const terrain::HorizonIndex first = level.firstSlot();
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                const terrain::unreal::HorizonTexel expected =
                    terrain::unreal::horizonTexel(level.atWindow(i, k), true);
                const auto at = static_cast<std::size_t>(
                    (slotOf(first.z, k, 129) * 129 + slotOf(first.x, i, 129)) *
                    terrain::unreal::kTexelFloats);
                const std::vector<float>& t = textures.texels[n];
                match = match && t[at] == expected.height && t[at + 1] == expected.gradX &&
                        t[at + 2] == expected.gradZ && t[at + 3] == expected.color;
            }
        }
    }
    TEST_CHECK(match);
    const terrain::HorizonLevel& level0 = clipmap.level(0);
    // The first detail texel of the sample nearest to (x, z), and its cell.
    const auto detail = [&](double x, double z) {
        const terrain::HorizonIndex sample = terrain::canonicalCenter(clipmap.params(), 0, x, z);
        const terrain::HorizonRect window = level0.window();
        const auto i =
            static_cast<int>(terrain::wrapIndex(sample.x - window.x0, level0.counts().x));
        const auto k =
            static_cast<int>(terrain::wrapIndex(sample.z - window.z0, level0.counts().z));
        const std::uint16_t* texel =
            textures.normalAt(0, slotOf(level0.firstSlot().x, i, 129) * textures.ratio,
                              slotOf(level0.firstSlot().z, k, 129) * textures.ratio);
        return std::pair{texel, level0.atWindow(i, k)};
    };
    // Computed at the player; in the middle of the square, the slope of the grid at the sample.
    TEST_CHECK(detail(kX, kZ).first[0] != terrain::unreal::kNormalPendingHalf);
    const auto [middle, cell] = detail(kX, kZ - 300.0);
    TEST_CHECK(middle[0] == terrain::unreal::halfFromFloat(cell.gradX) &&
               middle[1] == terrain::unreal::halfFromFloat(cell.gradZ));
    TEST_CHECK(clipmap.level(0)
                   .find(terrain::canonicalCenter(clipmap.params(), 0, kX, kZ - 300.0))
                   ->height < session.tiles().heightfield().heightAt(kX, kZ - 300.0));
}

// The levels whose bodies an update rebuilt.
std::set<int> bodiesOf(const terrain::unreal::HorizonUpdate& update) {
    std::set<int> bodies;
    for (const terrain::unreal::HorizonUpload& upload : update.uploads) {
        if (upload.section == terrain::unreal::HorizonSection::Body) {
            bodies.insert(upload.level);
        }
    }
    return bodies;
}

// An explosion 3 km away, beyond the windows of levels 0 to 2: the bodies of levels 3 and 4.
// Another just beyond the east border of level 2, which is stitched to the samples of level 3
// written there: its body too.
void testCpuPath() {
    TerrainSession session(sessionParams());
    const UnrealFrame frame = session.frameAt(kX, kZ);
    terrain::unreal::HorizonMeshParams params;
    params.levels = 5;
    params.cellsPerSide = 128;
    params.workerThreads = 0;
    params.lod = {{1, 3, 7}, 23};
    terrain::unreal::HorizonMeshStreamer streamer(
        session.tiles().heightfield(), session.tiles().ecology(), session.tiles().world(), params,
        &session.horizonEdits());
    const auto settle = [&] {
        terrain::unreal::HorizonUpdate last = streamer.update({0.0, 0.0, 0.0}, frame);
        while (streamer.streamer().busy()) {
            streamer.waitIdle();
            terrain::unreal::HorizonUpdate next = streamer.update({0.0, 0.0, 0.0}, frame);
            if (next.committed) {
                last = std::move(next);
            }
        }
        return last;
    };
    settle();
    const terrain::Heightfield& field = session.tiles().heightfield();
    session.applyEdit(terrain::unreal::groundEditAt(field, {GroundEdit::Blast, 0.0, 40.0, 10.0},
                                                    kX + 3000.0, kZ));
    const terrain::unreal::HorizonUpdate far = settle();
    const terrain::HorizonLevel& level2 = streamer.streamer().level(2);
    const double border = level2.originX(kX) + (level2.side() - 1) * level2.pitch();
    session.applyEdit(terrain::unreal::groundEditAt(field, {GroundEdit::Blast, 0.0, 16.0, 10.0},
                                                    border + 90.0, kZ));
    const terrain::unreal::HorizonUpdate near = settle();
    std::printf("explosions 3 km away and 90 m beyond the border of level 2: %lld and %lld "
                "samples, %zu and %zu bodies\n",
                static_cast<long long>(far.samples), static_cast<long long>(near.samples),
                bodiesOf(far).size(), bodiesOf(near).size());
    TEST_CHECK(far.committed && far.samples > 0 && far.coarsestMoved == -1);
    TEST_CHECK(bodiesOf(far) == (std::set<int>{3, 4}));
    TEST_CHECK(near.committed && bodiesOf(near) == (std::set<int>{2, 3, 4}));
}

} // namespace

int main() {
    testCommands();
    testSession();
    testGpuPath();
    testCpuPath();
    return test::result();
}
