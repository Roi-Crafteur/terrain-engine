// The walls of the slots of the falls in steps of the small world (phase 12A): one wall from their
// water up to the relief, the frame of the slot faded back to the straight axis on it.
//   - Frame: frameSide (river_winding.hpp) for offsets of the slot of 0 to 60 m either way,
//     stretches of 1 to kStretchMost and inner half widths of 3 to 30 m, every kFrameStep meters
//     across: within the slot its side from the middle of the slot over the stretch, beyond
//     frameOuter its side from the axis, between them growing all the way (no fold) by
//     (1 - 1.5 w / band) / stretch per meter at least (w the offset, band the fade), and by
//     kSlopeMost per meter at most (no jump).
//   - One wall: across the slot down the stretches in steps, from its channel to the relief, the
//     carved ground never has a second break of slope above the lip of the slot (a terrace above
//     its rim, then a second wall): kTerracesMost per thousand sides at most.
#include "falls_support.hpp"
#include "river_winding.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using falls::Reaches;
using falls::World;

constexpr double kRunGap = 20.0; // the points of a corridor: closer than this
constexpr double kFrameStep = 0.01;
constexpr double kFrameBeyond = 5.0; // meters beyond frameOuter on each side
constexpr double kSlopeMost = 4.0;
constexpr double kSlopeRounding = 1e-6;

void testFrame() {
    namespace water = terrain::water;
    std::size_t cases = 0;
    std::size_t folds = 0;
    std::size_t inside = 0;
    std::size_t outside = 0;
    std::size_t jumps = 0;
    double least = std::numeric_limits<double>::infinity();
    for (const double offset : {-60.0, -25.0, -8.0, -1.0, 0.0, 3.0, 12.0, 40.0, 60.0}) {
        for (const double stretch : {1.0, 1.15, water::kStretchMost}) {
            for (const double inner : {3.0, 12.0, 30.0}) {
                ++cases;
                const double w = std::abs(offset);
                const double from = w + inner * stretch;
                const double band = std::max(w + water::kFadeBand, water::kFadeShare * w);
                const double floor = (1.0 - 1.5 * w / band) / stretch;
                const double outer = water::frameOuter(offset, stretch, inner);
                const auto steps = static_cast<int>(2.0 * (outer + kFrameBeyond) / kFrameStep);
                double before = water::frameSide(offset, stretch, inner, -outer - kFrameBeyond);
                for (int k = 1; k <= steps; ++k) {
                    const double side = -outer - kFrameBeyond + k * kFrameStep;
                    const double f = water::frameSide(offset, stretch, inner, side);
                    const double slope = (f - before) / kFrameStep;
                    folds += slope > 0.0 ? 0 : 1;
                    jumps += slope > kSlopeMost ? 1 : 0;
                    least = std::min(least, slope / floor);
                    const bool off = std::abs(f - (side - offset) / stretch) > 1e-9;
                    inside += std::abs(side) <= from && off ? 1 : 0;
                    outside += std::abs(side) >= outer && f != side ? 1 : 0;
                    before = f;
                }
            }
        }
    }
    std::printf("the frame of %zu slots across: %zu folds, %zu jumps, %zu off the slot within it, "
                "%zu off the axis beyond it; its slope %.3f times its least bound at least\n",
                cases, folds, jumps, inside, outside, least);
    TEST_CHECK(folds == 0 && jumps == 0);
    TEST_CHECK(inside == 0 && outside == 0);
    TEST_CHECK(least >= 1.0 - kSlopeRounding);
}

// The walls across the slots of the stretches in steps (their points where the slot cuts more than
// kTerraceDeep meters under the floor of the valley), every other point, on each side from the
// edge of the channel to kTerraceFar meters every kTerraceStep: a terrace is a run of kTerraceRun
// meters at least sloping less than kTerraceFlat, kTerraceAbove meters over the water at least
// (above the ledges of the slot along it), dry and carved into the relief, after which the carved
// ground rises kTerraceRise meters more as a wall (kTerraceWall meters per meter at least
// somewhere: not a gentle slope), before the relief or other water (another valley).
constexpr double kTerraceDeep = 3.0;
constexpr double kTerraceFar = 70.0;
constexpr double kTerraceStep = 1.0;
constexpr double kTerraceRun = 4.0;
constexpr double kTerraceFlat = 0.25;
constexpr double kTerraceAbove = 3.0;
constexpr double kTerraceRise = 3.0;
constexpr double kTerraceWall = 1.0;
constexpr double kTerracesMost = 5.0;

bool terraceOn(const std::vector<terrain::HeightSample>& ground, const std::vector<float>& relief,
               double water) {
    std::size_t flat = 0;
    double level = std::numeric_limits<double>::quiet_NaN();
    bool wall = false;
    for (std::size_t k = 1; k + 1 < ground.size(); ++k) {
        const double h = ground[k].height;
        if (!(h < static_cast<double>(relief[k]) - 0.5) ||
            ground[k].water != terrain::WaterKind::None) {
            return false; // the relief or other water: the end of the valley
        }
        if (wall && h > level + kTerraceRise) {
            return true;
        }
        const double slope = std::abs(ground[k + 1].height - h) / kTerraceStep;
        wall = wall || (!std::isnan(level) && slope >= kTerraceWall);
        flat = slope < kTerraceFlat && h > water + kTerraceAbove ? flat + 1 : 0;
        if (std::isnan(level) && static_cast<double>(flat) * kTerraceStep >= kTerraceRun) {
            level = h;
        }
    }
    return false;
}

void testOneWall(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::size_t sides = 0;
    std::size_t terraces = 0;
    const auto n = static_cast<std::size_t>(kTerraceFar / kTerraceStep);
    std::vector<double> xs(n);
    std::vector<double> zs(n);
    std::vector<terrain::HeightSample> ground(n);
    std::vector<float> relief(n);
    for (const std::size_t r : steeps) {
        const auto& p = reaches[r].points;
        for (std::size_t i = 1; i + 1 < p.size(); i += 2) {
            const double dx = p[i + 1].x - p[i - 1].x;
            const double dz = p[i + 1].z - p[i - 1].z;
            const double length = std::hypot(dx, dz);
            if (!(falls::depthAt(reaches[r], i) > kTerraceDeep) || !(length > 0.0) ||
                !(length < 2.0 * kRunGap)) {
                continue;
            }
            for (const double side : {-1.0, 1.0}) {
                for (std::size_t k = 0; k < n; ++k) {
                    const double d = 0.5 * static_cast<double>(p[i].width) +
                                     static_cast<double>(k) * kTerraceStep;
                    xs[k] = p[i].x - side * d * dz / length;
                    zs[k] = p[i].z + side * d * dx / length;
                }
                w.field.samplesAt(xs, zs, ground);
                w.relief.heightsAt(xs, zs, relief);
                ++sides;
                terraces += terraceOn(ground, relief, p[i].level) ? 1 : 0;
            }
        }
    }
    const double perThousand =
        1000.0 * static_cast<double>(terraces) / static_cast<double>(std::max<std::size_t>(1, sides));
    std::printf("across the slots of the stretches in steps: %zu sides, %zu with a terrace above "
                "the lip and a second wall (%.1f per thousand)\n",
                sides, terraces, perThousand);
    TEST_CHECK(sides >= 1000);
    TEST_CHECK(perThousand <= kTerracesMost);
}

} // namespace

int main() {
    testFrame();
    const World w = falls::makeWorld();
    Reaches reaches(*w.hydrology);
    const std::vector<std::size_t> steeps = falls::steepReaches(w, reaches);
    testOneWall(w, reaches, steeps);
    return test::result();
}
