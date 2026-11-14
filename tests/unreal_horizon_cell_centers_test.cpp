// The cells of the grids of the GPU horizon cut along their contour lines (agent 40,
// Bridge/horizon_split.hpp): withCellCenters gives every cell of samples a vertex of its own; left
// on its corner (i + 1, k), as AdaptiveSplit 0 leaves it, the triangles drawn are exactly those of
// the grids before (the fixed diagonal, the same orientation), the two others of no area; moved
// onto the corner (i, k), they are those of the main diagonal. The skirts and the sea square are
// kept. Then the rule the vertex shader follows (centerOnMain, coarserOnMain).
#include "horizon_mesh.hpp"
#include "horizon_split.hpp"
#include "horizon_textures.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
    TEST_CHECK(centers > 0);
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
    return test::result();
}
