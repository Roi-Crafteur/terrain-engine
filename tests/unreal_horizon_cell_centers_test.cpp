// The cells of the grids of the GPU horizon cut along their contour lines (agent 40,
// Bridge/horizon_split.hpp): withCellCenters gives every cell of samples a vertex of its own; left
// on its corner (i + 1, k), as AdaptiveSplit 0 leaves it, the triangles drawn are exactly those of
// the grids before (the fixed diagonal, the same orientation), the two others of no area; moved
// onto the corner (i, k), they are those of the main diagonal. The skirts and the sea square are
// kept. Then the rule the vertex shader follows (centerOnMain, coarserOnMain), and the levels
// with centers (levelHasCenters, coarseSplit).
#include "horizon_mesh.hpp"
#include "horizon_split.hpp"
#include "horizon_textures.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using terrain::unreal::GridMesh;
using terrain::unreal::GridVertexKind;
using Triangle = std::array<std::int32_t, 3>;

// A triangle by the positions of its corners, its first the lowest index of position: the same
// triangle whatever vertex stands at a place, its orientation kept.
using Place = std::pair<float, float>;
using Shape = std::array<Place, 3>;

Shape shapeOf(const GridMesh& mesh, const Triangle& t) {
    Shape s{};
    for (std::size_t c = 0; c < 3; ++c) {
        const auto& v = mesh.vertices[static_cast<std::size_t>(t[c])];
        s[c] = {v.i, v.k};
    }
    const auto first = std::min_element(s.begin(), s.end()) - s.begin();
    std::rotate(s.begin(), s.begin() + first, s.end());
    return s;
}

bool flat(const Shape& s) {
    return s[0] == s[1] || s[1] == s[2] || s[0] == s[2];
}

// The triangles drawn, the center of each cell moved by (di, dk) cells from its corner (i + 1, k):
// (0, 0) as AdaptiveSplit 0 leaves it, (-1, 0) onto the corner (i, k). Those of no area left out.
std::vector<Shape> drawn(GridMesh mesh, float di, float dk) {
    for (auto& v : mesh.vertices) {
        if (v.kind == GridVertexKind::Center) {
            v.i += di;
            v.k += dk;
        }
    }
    std::vector<Shape> out;
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const Shape s = shapeOf(mesh, {mesh.triangles[3 * t], mesh.triangles[3 * t + 1],
                                       mesh.triangles[3 * t + 2]});
        if (!flat(s)) {
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Shape> shapes(const GridMesh& mesh) {
    return drawn(mesh, 0.0f, 0.0f);
}

// The orientation of a triangle: Unreal draws the front of (a, b, c) on the side of
// (c - a) x (b - a), up as the cells of the grids (unreal_horizon_textures_test).
bool up(const Shape& s) {
    return (s[2].first - s[0].first) * (s[1].second - s[0].second) -
               (s[2].second - s[0].second) * (s[1].first - s[0].first) >
           0.0f;
}

void testGrid(const GridMesh& before) {
    const GridMesh centered = terrain::unreal::withCellCenters(before);
    std::size_t centers = 0;
    for (const auto& v : centered.vertices) {
        centers += v.kind == GridVertexKind::Center ? 1 : 0;
    }
    // Every cell of samples has its center: two triangles each before, four now.
    TEST_CHECK(centers > 0 && centers == terrain::unreal::cellCenterCount(before));
    TEST_CHECK(centered.vertices.size() == before.vertices.size() + centers);
    TEST_CHECK(centered.triangleCount() == before.triangleCount() + 2 * centers);
    TEST_CHECK(std::equal(before.vertices.begin(), before.vertices.end(),
                          centered.vertices.begin()));
    // Left on its corner (i + 1, k): the triangles of the grid before, exactly.
    TEST_CHECK(shapes(centered) == shapes(before));
    // Moved onto the corner (i, k): the cells cut along their main diagonal, facing up, the other
    // triangles (skirts, sea square) as before.
    const std::vector<Shape> moved = drawn(centered, -1.0f, 0.0f);
    TEST_CHECK(moved.size() == shapes(before).size());
    std::size_t mainTriangles = 0;
    for (const Shape& s : moved) {
        const float i = std::min({s[0].first, s[1].first, s[2].first});
        const float k = std::min({s[0].second, s[1].second, s[2].second});
        const Place corner{i, k}, across{i + 1.0f, k + 1.0f};
        const bool both = std::count(s.begin(), s.end(), corner) == 1 &&
                          std::count(s.begin(), s.end(), across) == 1;
        mainTriangles += both ? 1 : 0;
    }
    TEST_CHECK(mainTriangles >= 2 * centers);
    // The cells keep the orientation of the grid (the skirts face inward: not counted).
    std::size_t cellsUp = 0, cellsAll = 0;
    for (std::size_t t = 0; t < centered.triangleCount(); ++t) {
        const Triangle tri{centered.triangles[3 * t], centered.triangles[3 * t + 1],
                           centered.triangles[3 * t + 2]};
        if (centered.vertices[static_cast<std::size_t>(tri[2])].kind != GridVertexKind::Center) {
            continue;
        }
        GridMesh one = centered;
        for (auto& v : one.vertices) {
            if (v.kind == GridVertexKind::Center) {
                v.i -= 1.0f;
            }
        }
        const Shape s = shapeOf(one, tri);
        if (!flat(s)) {
            ++cellsAll;
            cellsUp += up(s) ? 1 : 0;
        }
    }
    TEST_CHECK(cellsAll == 2 * centers && cellsUp == cellsAll);
}

void testRule() {
    using terrain::unreal::CenterInput;
    using terrain::unreal::centerOnMain;
    // A wall along the main diagonal: the corners 00 and 11 alike, 10 high, 01 low.
    const std::array<float, 4> wall{50.0f, 100.0f, 0.0f, 50.0f};
    const std::array<float, 4> across{50.0f, 50.0f, 50.0f, 100.0f}; // the other way
    TEST_CHECK(!centerOnMain(CenterInput{false, wall}));           // without the switch: fixed
    TEST_CHECK(centerOnMain(CenterInput{true, wall}));
    TEST_CHECK(!centerOnMain(CenterInput{true, across}));
    TEST_CHECK(!centerOnMain(CenterInput{true, {1.0f, 2.0f, 2.0f, 1.0f}})); // a tie: fixed
    // Past the middle of the morph, a cell along the diagonal of the coarser cell takes it.
    CenterInput morph{true, wall, true, across, 0, 0, 0.4};
    TEST_CHECK(centerOnMain(morph)); // before the middle: its own
    morph.morphAtCenter = 0.6;
    TEST_CHECK(centerOnMain(morph)); // the coarser one is fixed, this cell off its diagonal
    morph.quarterI = 1;              // (1, 0): along the fixed diagonal of the coarser cell
    TEST_CHECK(!centerOnMain(morph));
    CenterInput coarseMain{true, across, true, wall, 1, 1, 1.0};
    TEST_CHECK(centerOnMain(coarseMain)); // (1, 1) along the main diagonal of the coarser cell
    coarseMain.quarterK = 0;
    TEST_CHECK(!centerOnMain(coarseMain)); // off it: its own
    coarseMain.adaptive = false;
    TEST_CHECK(!centerOnMain(coarseMain));
    TEST_CHECK(!terrain::unreal::coarserOnMain(false, wall));
    TEST_CHECK(terrain::unreal::coarserOnMain(true, wall));
    TEST_CHECK(!terrain::unreal::coarserOnMain(true, across));
}

// The surface of a level at the end of its morph over one cell of the coarser level (corners c:
// 00, 10, 01, 11) at (U, V) in that cell: its 2 x 2 cells, their samples on their morph targets
// (horizon_morph.hpp: the mean of the ends of an edge; at the middle, of the diagonal the coarser
// cell is cut along, coarserOnMain), each cut as centerOnMain says.
double fineSurface(const std::array<float, 4>& c, bool coarseAdaptive, double U, double V) {
    const bool coarseMain = terrain::unreal::coarserOnMain(coarseAdaptive, c);
    const auto corner = [&](int i, int k) { return static_cast<double>(c[2 * k + i]); };
    const auto at = [&](int i, int k) {
        if (i % 2 == 0 && k % 2 == 0) {
            return corner(i / 2, k / 2);
        }
        if (i == 1 && k == 1) {
            return coarseMain ? 0.5 * (corner(0, 0) + corner(1, 1))
                              : 0.5 * (corner(1, 0) + corner(0, 1));
        }
        return i == 1 ? 0.5 * (corner(0, k / 2) + corner(1, k / 2))
                      : 0.5 * (corner(i / 2, 0) + corner(i / 2, 1));
    };
    const int fi = std::min(static_cast<int>(U * 2.0), 1);
    const int fk = std::min(static_cast<int>(V * 2.0), 1);
    const std::array<double, 4> h{at(fi, fk), at(fi + 1, fk), at(fi, fk + 1), at(fi + 1, fk + 1)};
    terrain::unreal::CenterInput in{true,
                                    {static_cast<float>(h[0]), static_cast<float>(h[1]),
                                     static_cast<float>(h[2]), static_cast<float>(h[3])},
                                    true, c, fi, fk, 1.0, coarseAdaptive};
    const auto w = terrain::unreal::cellWeights(terrain::unreal::centerOnMain(in), U * 2.0 - fi,
                                                V * 2.0 - fk);
    return w[0] * h[0] + w[1] * h[1] + w[2] * h[2] + w[3] * h[3];
}

double coarseSurface(const std::array<float, 4>& c, bool main, double U, double V) {
    const auto w = terrain::unreal::cellWeights(main, U, V);
    return w[0] * c[0] + w[1] * c[1] + w[2] * c[2] + w[3] * c[3];
}

// The centers on the levels 0 to 3 only (HorizonCenterLevels 4, the fallback of the cost): the
// level 3 meets the level 4, drawn with the fixed diagonal, with no crack once its odd samples and
// its centers follow that diagonal (CoarseSplit 0); following the contour of the coarser cell as
// before, it would leave a crack of up to a quarter of the step across the cell.
void testLevelsWithoutCenters() {
    using terrain::unreal::coarseSplit;
    using terrain::unreal::levelHasCenters;
    constexpr int kLevels = 10;
    for (int level = 0; level < kLevels; ++level) {
        TEST_CHECK(levelHasCenters(level, terrain::unreal::kHorizonCenterLevels) == (level < 4));
        TEST_CHECK(coarseSplit(level, terrain::unreal::kHorizonCenterLevels) == (level < 3));
        TEST_CHECK(!levelHasCenters(level, 0) && levelHasCenters(level, kLevels));
    }
    TEST_CHECK(!coarseSplit(kLevels - 1, kLevels)); // the coarsest level has no coarser one
    // The grids built: with 4 levels of centers, level 0 with them and the ring and trims in both
    // forms (levels 1 to 3 with, 4 to 9 without); every one for the tools of the developers.
    using terrain::unreal::GridSet;
    using terrain::unreal::gridsNeeded;
    TEST_CHECK((gridsNeeded(4, kLevels, false) == GridSet{true, false, true, true}));
    TEST_CHECK((gridsNeeded(kLevels, kLevels, false) == GridSet{true, false, true, false}));
    TEST_CHECK((gridsNeeded(1, kLevels, false) == GridSet{true, false, false, true}));
    TEST_CHECK((gridsNeeded(0, kLevels, false) == GridSet{false, true, false, true}));
    TEST_CHECK((gridsNeeded(4, kLevels, true) == GridSet{true, true, true, true}));
    // A cell that bends: its contour runs along the main diagonal (|0 - 10| < |100 - 80|), whose
    // ends meet at 5 m in the middle, those of the fixed one at 90 m.
    const std::array<float, 4> wall{0.0f, 100.0f, 80.0f, 10.0f};
    double fixedGap = 0.0, contourGap = 0.0, oldCrack = 0.0;
    for (int a = 0; a <= 16; ++a) {
        for (int b = 0; b <= 16; ++b) {
            const double U = a / 16.0, V = b / 16.0;
            fixedGap = std::max(fixedGap, std::abs(fineSurface(wall, false, U, V) -
                                                   coarseSurface(wall, false, U, V)));
            contourGap = std::max(contourGap, std::abs(fineSurface(wall, true, U, V) -
                                                       coarseSurface(wall, true, U, V)));
            oldCrack = std::max(oldCrack, std::abs(fineSurface(wall, true, U, V) -
                                                   coarseSurface(wall, false, U, V)));
        }
    }
    std::printf("the end of the morph against the coarser level: %.2e m toward one without "
                "centers (CoarseSplit 0), %.2e m toward one with them; %.1f m of crack with the "
                "contour toward one without them\n",
                fixedGap, contourGap, oldCrack);
    TEST_CHECK(fixedGap < 1e-9 && contourGap < 1e-9 && oldCrack > 10.0);
}

} // namespace

int main() {
    constexpr int kCells = 64;
    const terrain::unreal::CellRect hole = terrain::unreal::centeredSquare(kCells, 17);
    testGrid(terrain::unreal::bodyGrid(terrain::unreal::bodyTopology(kCells, hole, true), 12.0));
    testGrid(terrain::unreal::bodyGrid(
        terrain::unreal::bodyTopology(kCells, terrain::unreal::CellRect{}, false), 0.0));
    for (int variant = 0; variant < terrain::unreal::kTrimVariants; ++variant) {
        testGrid(terrain::unreal::trimGrid(kCells, variant % 3 - 1, variant / 3 - 1));
    }
    testRule();
    testLevelsWithoutCenters();
    return test::result();
}
