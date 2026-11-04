#include "horizon_split.hpp"

#include <cmath>

namespace terrain::unreal {

bool mainDiagonal(CellSplit split, float h00, float h10, float h01, float h11) {
    return split == CellSplit::Contour && std::abs(h00 - h11) < std::abs(h10 - h01);
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
