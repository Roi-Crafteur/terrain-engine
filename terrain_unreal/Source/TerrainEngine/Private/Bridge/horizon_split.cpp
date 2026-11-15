#include "horizon_split.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain::unreal {

bool mainDiagonal(CellSplit split, float h00, float h10, float h01, float h11) {
    return split == CellSplit::Contour && std::abs(h00 - h11) < std::abs(h10 - h01);
}

namespace {

// Whether the triangles tri and tri + 1 are the two of a cell of samples: {00, 01, 10} then
// {10, 01, 11}, its corners one cell apart.
bool cellPair(const GridMesh& grid, std::size_t tri) {
    const auto at = [&](std::size_t n) {
        return grid.vertices[static_cast<std::size_t>(grid.triangles[n])];
    };
    const std::size_t t = 3 * tri;
    if (grid.triangles[t + 3] != grid.triangles[t + 2] ||
        grid.triangles[t + 4] != grid.triangles[t + 1]) {
        return false;
    }
    const GridVertex v00 = at(t), v01 = at(t + 1), v10 = at(t + 2), v11 = at(t + 5);
    for (const GridVertex& v : {v00, v01, v10, v11}) {
        if (v.kind != GridVertexKind::Sample) {
            return false;
        }
    }
    return v10.i == v00.i + 1.0f && v10.k == v00.k && v01.i == v00.i && v01.k == v00.k + 1.0f &&
           v11.i == v00.i + 1.0f && v11.k == v00.k + 1.0f;
}

} // namespace

GridMesh withCellCenters(const GridMesh& grid) {
    GridMesh out;
    out.vertices = grid.vertices;
    out.triangles.reserve(grid.triangles.size() * 2);
    const std::size_t count = grid.triangleCount();
    for (std::size_t tri = 0; tri < count; ++tri) {
        if (tri + 1 < count && cellPair(grid, tri)) {
            const std::size_t t = 3 * tri;
            const std::int32_t v00 = grid.triangles[t], v01 = grid.triangles[t + 1];
            const std::int32_t v10 = grid.triangles[t + 2], v11 = grid.triangles[t + 5];
            const auto c = static_cast<std::int32_t>(out.vertices.size());
            const GridVertex corner = grid.vertices[static_cast<std::size_t>(v10)];
            out.vertices.push_back({corner.i, corner.k, GridVertexKind::Center});
            out.triangles.insert(out.triangles.end(),
                                 {v00, v01, c, v01, v11, c, v11, v10, c, v10, v00, c});
            ++tri;
            continue;
        }
        out.triangles.insert(out.triangles.end(), grid.triangles.begin() + 3 * tri,
                             grid.triangles.begin() + 3 * tri + 3);
    }
    return out;
}

std::size_t cellCenterCount(const GridMesh& grid) {
    std::size_t centers = 0;
    const std::size_t count = grid.triangleCount();
    for (std::size_t tri = 0; tri + 1 < count; ++tri) {
        if (cellPair(grid, tri)) {
            ++centers;
            ++tri;
        }
    }
    return centers;
}

bool centerOnMain(const CenterInput& in) {
    if (!in.adaptive) {
        return false;
    }
    const bool own = mainDiagonal(CellSplit::Contour, in.own[0], in.own[1], in.own[2], in.own[3]);
    if (!in.hasCoarser || in.morphAtCenter < 0.5) {
        return own;
    }
    const bool coarse = coarserOnMain(in.coarseAdaptive, in.coarse);
    const bool alongMain = in.quarterI == in.quarterK;
    if (coarse == alongMain) {
        return coarse; // along the diagonal the coarser cell is cut along
    }
    return own;
}

bool coarserOnMain(bool adaptive, const std::array<float, 4>& coarse) {
    return mainDiagonal(adaptive ? CellSplit::Contour : CellSplit::Fixed, coarse[0], coarse[1],
                        coarse[2], coarse[3]);
}

bool levelHasCenters(int level, int centerLevels) {
    return level >= 0 && level < centerLevels;
}

bool coarseSplit(int level, int centerLevels) {
    return levelHasCenters(level + 1, centerLevels);
}

GridSet gridsNeeded(int centerLevels, int levels, bool every) {
    GridSet set;
    set.level0Centered = every || levelHasCenters(0, centerLevels);
    set.level0Plain = every || !levelHasCenters(0, centerLevels);
    set.ringCentered = every || (levels > 1 && levelHasCenters(1, centerLevels));
    set.ringPlain = every || (levels > 1 && !levelHasCenters(levels - 1, centerLevels));
    return set;
}

std::array<double, 4> cellWeights(bool main, double u, double v) {
    if (main) {
        // The triangles (00, 10, 11) below the diagonal (u >= v) and (00, 01, 11) above it.
        return u >= v ? std::array<double, 4>{1.0 - u, u - v, 0.0, v}
                      : std::array<double, 4>{1.0 - v, 0.0, v - u, u};
    }
    // The triangles (00, 10, 01) before the diagonal (u + v <= 1) and (10, 01, 11) beyond it.
    return u + v <= 1.0 ? std::array<double, 4>{1.0 - u - v, u, v, 0.0}
                        : std::array<double, 4>{0.0, 1.0 - v, 1.0 - u, u + v - 1.0};
}

} // namespace terrain::unreal
