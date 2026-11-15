// A hole of one block and a wall one block thick seen from the west, as a player streams the tiles
// (agent 41, tools/edit_views.hpp; a control, not a renderer: the check in Unreal stays), under a
// sun at 30 degrees and a low one at 6 degrees:
// - when the tile of the edit switches from LOD 0 to 1 (the eye 60 m away, the tiles selected for
//   a player 3 then 4 chunks away), the edit keeps its look: it does not vanish, most of its
//   outline at LOD 0 stays within 2 pixels (0.1 m there), nothing shows beyond a cell of LOD 1
//   (2 m: the relay of a block spans the cells around its column) from it, and its mean luminance
//   moves less than 8 / 255. Pixel by pixel the two views differ more (printed): the ground of
//   the coarse tiles around moves the line of a hole by a row, the pit of a hole is 4 m across
//   instead of 2, and the shades of a face differ by a few levels;
// - from 100 to 300 m it fades out progressively against the same edit on tiles of LOD 0: at
//   LOD 1 most of its pixels, at LOD 2 some (its relay half its height), at LOD 3 none.
#include "edit_views.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using edit_views::Edit;
using edit_views::Scene;
using edit_views::View;

constexpr int kOutline = 2;           // pixels
constexpr double kOutlineKept = 0.8;   // of the pixels of the edit at LOD 0
constexpr double kWithinCell = 0.95;   // of the pixels of the edit at LOD 1
constexpr double kLuminance = 8.0;     // of 255

void testSwitch(Scene& scene, const char* name, double sunSine) {
    const terrain::ChunkCoord edit = terrain::worldToChunk(scene.x(), scene.z());
    View fine{60.0, sunSine, 640, 360, terrain::ChunkCoord{edit.x - 3, edit.z}};
    View coarse = fine;
    coarse.player = terrain::ChunkCoord{edit.x - 4, edit.z};
    TEST_CHECK(edit_views::editLod(scene, fine) == 0 && edit_views::editLod(scene, coarse) == 1);
    const terrain::Image fineWith = edit_views::render(scene, fine, true);
    const terrain::Image fineWithout = edit_views::render(scene, fine, false);
    const terrain::Image coarseWith = edit_views::render(scene, coarse, true);
    const terrain::Image coarseWithout = edit_views::render(scene, coarse, false);
    const int cell = static_cast<int>(
        std::ceil(terrain::tileCellMeters(1) * edit_views::pixelsPerMeter(fine)));
    const edit_views::Switch look =
        edit_views::compareSwitch(fineWith, fineWithout, coarseWith, coarseWithout, kOutline);
    const edit_views::Switch far =
        edit_views::compareSwitch(fineWith, fineWithout, coarseWith, coarseWithout, cell);
    const std::size_t changed =
        edit_views::changedPixels(fineWith, fineWithout, coarseWith, coarseWithout);
    std::printf("%s at 60 m, sun %.1f: %zu pixels at LOD 0, %zu at LOD 1; %zu of LOD 0 within %d "
                "pixels of LOD 1, %zu of LOD 1 within %d of LOD 0; luminance %.1f, %.1f; %zu "
                "pixels changed\n",
                name, sunSine, look.shownA, look.shownB, look.nearA, kOutline, far.nearB, cell,
                look.lumaA, look.lumaB, changed);
    const auto share = [](std::size_t part, std::size_t whole) {
        return static_cast<double>(part) / static_cast<double>(std::max<std::size_t>(whole, 1));
    };
    TEST_CHECK(look.shownA > 0 && 2 * look.shownB >= look.shownA);
    TEST_CHECK(share(look.nearA, look.shownA) >= kOutlineKept);
    TEST_CHECK(share(far.nearB, look.shownB) >= kWithinCell);
    TEST_CHECK(std::abs(look.lumaA - look.lumaB) <= kLuminance);
}

// The pixels of the edit kept against tiles of LOD 0 at the same distance.
constexpr double kLod1Kept = 0.7;
constexpr double kLod2Kept = 0.1;

void testFade(Scene& scene, const char* name, double sunSine) {
    const terrain::ChunkCoord edit = terrain::worldToChunk(scene.x(), scene.z());
    std::size_t previous = 0;
    bool first = true;
    for (const double distance : {100.0, 150.0, 200.0, 256.0, 300.0}) {
        const View view{distance, sunSine, 640, 360, std::nullopt};
        View fine = view;
        fine.player = edit; // the tiles of LOD 0 over the edit: how it looks without coarse tiles
        const int lod = edit_views::editLod(scene, view);
        const std::size_t shown = edit_views::editPixels(edit_views::render(scene, view, true),
                                                         edit_views::render(scene, view, false));
        const std::size_t truth = edit_views::editPixels(edit_views::render(scene, fine, true),
                                                         edit_views::render(scene, fine, false));
        std::printf("%s at %.0f m (LOD %d), sun %.1f: %zu pixels, %zu at LOD 0\n", name, distance,
                    lod, sunSine, shown, truth);
        TEST_CHECK(first || shown <= previous);
        const double kept = static_cast<double>(shown);
        const double whole = static_cast<double>(truth);
        if (lod == 1) {
            TEST_CHECK(kept >= kLod1Kept * whole);
        } else if (lod == 2) {
            TEST_CHECK(kept >= kLod2Kept * whole && shown <= truth);
        } else if (lod == terrain::kMaxLod) {
            TEST_CHECK(shown == 0);
        }
        previous = shown;
        first = false;
    }
}

} // namespace

int main() {
    Scene hole(Edit::Hole, 44, 44);
    Scene wall(Edit::Wall, 45, 44);
    for (const double sun : {0.5, 0.1}) {
        testSwitch(hole, "a hole of one block", sun);
        testSwitch(wall, "a wall one block thick", sun);
        testFade(hole, "a hole of one block", sun);
        testFade(wall, "a wall one block thick", sun);
    }
    return test::result();
}
