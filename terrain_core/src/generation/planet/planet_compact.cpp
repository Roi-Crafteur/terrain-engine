#include "terrain/generation/planet/planet_level.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain {

namespace {

std::int16_t roundedElevation(float height) {
    const double rounded = std::floor(static_cast<double>(height) + 0.5);
    return static_cast<std::int16_t>(std::clamp(rounded, -32768.0, 32767.0));
}

std::uint16_t roundedPrecipitation(float precipitation) {
    const double rounded = std::floor(static_cast<double>(precipitation) + 0.5);
    return static_cast<std::uint16_t>(std::clamp(rounded, 0.0, 65535.0));
}

// The cells owned by the segments: every point but the last, which is the head of the segment
// joined, the sea, a lake or the ice; the last one too when it is the dry pit where the river
// ends (the first segment reaching it owns it).
std::vector<PlanetCells::RiverCell> riverCellsOf(const RiverGraph& graph) {
    std::vector<PlanetCells::RiverCell> cells;
    for (std::size_t s = 0; s < graph.segments.size(); ++s) {
        const RiverSegment& segment = graph.segments[s];
        const std::size_t owned =
            segment.vertices.size() - (segment.mouth == RiverMouth::ClosedBasin ? 0u : 1u);
        for (std::size_t v = 0; v < owned; ++v) {
            cells.push_back({segment.vertices[v].cell, static_cast<std::int32_t>(s),
                             static_cast<std::int32_t>(v)});
        }
    }
    std::stable_sort(cells.begin(), cells.end(),
                     [](const auto& a, const auto& b) { return a.cell < b.cell; });
    cells.erase(std::unique(cells.begin(), cells.end(),
                            [](const auto& a, const auto& b) { return a.cell == b.cell; }),
                cells.end());
    cells.shrink_to_fit();
    return cells;
}

template <class T> std::size_t bytesOf(const std::vector<T>& v) {
    return v.capacity() * sizeof(T);
}

} // namespace

std::int32_t PlanetCells::lakeAt(std::size_t cell) const {
    if ((bits[cell] & kLakeBit) == 0) {
        return -1;
    }
    const auto it = std::lower_bound(lakeCells.begin(), lakeCells.end(), cell,
                                     [](const LakeCell& a, std::size_t c) { return a.cell < c; });
    return it->lake;
}

const PlanetCells::RiverCell* PlanetCells::riverAt(std::size_t cell) const {
    if ((bits[cell] & kRiverBit) == 0) {
        return nullptr;
    }
    const auto it = std::lower_bound(riverCells.begin(), riverCells.end(), cell,
                                     [](const RiverCell& a, std::size_t c) { return a.cell < c; });
    return &*it;
}

std::size_t PlanetCells::memoryBytes() const {
    return bytesOf(rowTemperature) + bytesOf(elevation) + bytesOf(precipitation) + bytesOf(bits) +
           bytesOf(lakeCells) + bytesOf(riverCells);
}

namespace planet {

PlanetCells compactCells(const PlanetBuild& build) {
    PlanetCells cells;
    cells.grid = build.grid;
    cells.rowTemperature = build.climate.rowTemperature;
    const std::size_t count = build.grid.cells();
    cells.elevation.resize(count);
    cells.precipitation.resize(count);
    cells.bits.resize(count);
    for (std::size_t c = 0; c < count; ++c) {
        cells.elevation[c] = roundedElevation(build.height[c]);
        cells.precipitation[c] = roundedPrecipitation(build.climate.precipitation[c]);
        std::uint8_t bits = build.receiver[c];
        if (build.ocean[c] != kLand) {
            bits |= PlanetCells::kOceanBit;
        }
        if (build.lakeOfCell[c] >= 0) {
            bits |= PlanetCells::kLakeBit;
            cells.lakeCells.push_back({static_cast<std::uint32_t>(c), build.lakeOfCell[c]});
        }
        cells.bits[c] = bits;
    }
    cells.lakeCells.shrink_to_fit();
    cells.riverCells = riverCellsOf(build.rivers);
    for (const PlanetCells::RiverCell& river : cells.riverCells) {
        cells.bits[river.cell] |= PlanetCells::kRiverBit;
    }
    return cells;
}

} // namespace planet

} // namespace terrain
