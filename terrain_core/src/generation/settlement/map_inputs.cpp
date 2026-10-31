#include "map_inputs.hpp"

#include "../planet/planet_parallel.hpp"
#include "analysis_cell.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace terrain::settlement {

std::shared_ptr<const planet::LandIndex> landOf(const PlanetCells& cells) {
    std::vector<std::uint8_t> ocean(cells.bits.size());
    for (std::size_t c = 0; c < ocean.size(); ++c) {
        ocean[c] = cells.ocean(c) ? planet::kOcean : planet::kLand;
    }
    return std::make_shared<const planet::LandIndex>(ocean);
}

planet::LandValues<std::uint8_t>
waterFlags(const PlanetLevel& level, const std::shared_ptr<const planet::LandIndex>& land,
           const RiverClasses& rivers, int threads) {
    planet::LandValues<std::uint8_t> water(land, 0, 0);
    const PlanetGrid& grid = level.grid();
    const PlanetCells& cells = level.cells();
    planet::parallelRows(grid.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            const float row = cells.rowTemperature[static_cast<std::size_t>(j)];
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t c = grid.index(i, j);
                if (land->land(c) &&
                    planet::cellFrozen(row, level.elevation(c), level.params().climate)) {
                    water.at(c) = kFrozenBit;
                }
            }
        }
    });
    for (const PlanetCells::RiverCell& r : cells.riverCells) {
        const RiverSegment& s = level.rivers().segments[static_cast<std::size_t>(r.segment)];
        const double q = s.vertices[static_cast<std::size_t>(r.vertex)].discharge;
        if (land->land(r.cell)) {
            std::uint8_t& w = water.at(r.cell);
            const int known = w & kRiverClassMask;
            w = static_cast<std::uint8_t>((w & ~kRiverClassMask) |
                                          std::max(known, riverClass(q, rivers)));
        }
    }
    for (const PlanetCells::LakeCell& l : cells.lakeCells) {
        const PlanetLake& lake = level.lakes()[static_cast<std::size_t>(l.lake)];
        if (!land->land(l.cell)) {
            continue;
        }
        if (!lake.overflowing || lake.playa) {
            water.at(l.cell) |= kSaltLakeBit;
        } else if (!lake.frozen) {
            water.at(l.cell) |= kFreshLakeBit;
        }
    }
    const std::vector<std::int32_t> basins = level.basins();
    const auto all = static_cast<std::int32_t>(grid.cells());
    land->forEachLand([&](std::size_t c, std::size_t slot) {
        const std::int32_t b = basins[c];
        const bool closed =
            b >= all || (b >= 0 && !cells.ocean(static_cast<std::size_t>(b)) &&
                         cells.receiver(static_cast<std::size_t>(b)) == kNoNeighbor);
        if (closed) {
            water.values()[slot] |= kClosedBasinBit;
        }
    });
    return water;
}

} // namespace terrain::settlement
