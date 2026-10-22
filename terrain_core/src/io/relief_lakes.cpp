#include "terrain/io/relief_lakes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace terrain {

double deepLakeShare(const PlanetLevel& level, float depth) {
    const std::vector<PlanetLake>& lakes = level.lakes();
    std::vector<float> lowest(lakes.size(), std::numeric_limits<float>::max());
    std::vector<double> cells(lakes.size(), 0.0);
    for (std::size_t cell = 0; cell < level.grid().cells(); ++cell) {
        const std::int32_t lake = level.cells().lakeAt(cell);
        if (lake >= 0) {
            const auto k = static_cast<std::size_t>(lake);
            lowest[k] = std::min(lowest[k], level.elevation(cell));
            cells[k] += 1.0;
        }
    }
    double all = 0.0;
    double deep = 0.0;
    for (std::size_t k = 0; k < lakes.size(); ++k) {
        all += cells[k];
        deep += lakes[k].level - lowest[k] >= depth ? cells[k] : 0.0;
    }
    return all > 0.0 ? deep / all : 0.0;
}

} // namespace terrain
