#include "terrain/streaming/horizon_bands.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace terrain {

std::vector<RockBands> horizonBands(const Heightfield& field, const HorizonParams& params,
                                    int level, const HorizonRect& rect,
                                    std::span<const HorizonCell> cells) {
    if (cells.size() != static_cast<std::size_t>(rect.count())) {
        throw std::invalid_argument("horizonBands: cells of another count than the rectangle");
    }
    const int pitch = levelPitch(params, level);
    // The samples of bare rock, in or out of the water, row by row: their columns read in one
    // batch (Heightfield::samplesAt, the columns of rockAt bit for bit).
    std::vector<std::size_t> rocky;
    std::vector<double> xs;
    std::vector<double> zs;
    std::size_t n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++n) {
            if (cells[n].material == SurfaceMaterial::Rock) {
                rocky.push_back(n);
                xs.push_back(samplePosition(i, pitch));
                zs.push_back(samplePosition(k, pitch));
            }
        }
    }
    std::vector<HeightSample> samples(rocky.size());
    std::vector<RockColumn> columns(rocky.size());
    field.samplesAt(xs, zs, samples, columns);
    std::vector<RockBands> out(cells.size());
    for (std::size_t r = 0; r < rocky.size(); ++r) {
        if (columns[r].hasRock()) {
            out[rocky[r]] = rockBands(columns[r], static_cast<double>(cells[rocky[r]].height));
        }
    }
    return out;
}

} // namespace terrain
