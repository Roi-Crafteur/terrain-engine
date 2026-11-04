#include "terrain/streaming/horizon_bands.hpp"

#include <stdexcept>

namespace terrain {

std::vector<RockBands> horizonBands(const Heightfield& field, const HorizonParams& params,
                                    int level, const HorizonRect& rect,
                                    std::span<const HorizonCell> cells) {
    if (cells.size() != static_cast<std::size_t>(rect.count())) {
        throw std::invalid_argument("horizonBands: cells of another count than the rectangle");
    }
    const int pitch = levelPitch(params, level);
    std::vector<RockBands> out(cells.size());
    std::size_t n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
            const HorizonCell& cell = cells[n];
            if (cell.material != SurfaceMaterial::Rock || (cell.flags & kHorizonWater) != 0) {
                continue;
            }
            const RockColumn column =
                field.rockAt(samplePosition(i, pitch), samplePosition(k, pitch));
            if (column.hasRock()) {
                out[n] = rockBands(column, static_cast<double>(cell.height));
            }
        }
    }
    return out;
}

} // namespace terrain
