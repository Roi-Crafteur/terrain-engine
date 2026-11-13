#include "terrain/generation/planet/planet_network.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/runoff_partition.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terrain {

namespace {

constexpr std::uint8_t kKindMask = 0x03;
constexpr std::uint8_t kMainBit = 0x04;
constexpr int kBankShift = 3;
constexpr int kEndShift = 5;
constexpr double kHortonArea = 4.5; // the area ratio of a stream order to the next

// Where the water of a cell goes: the next cell, or -1 and its end; ends is the end of its
// watercourse there (a stream entering a lake ends there, its water goes on from the outlet).
struct NetworkNext {
    std::int64_t cell = -1;
    StreamEnd end = StreamEnd::Confluence;
};

NetworkNext lakeOutlet(const PlanetLevel& level, std::int32_t lake) {
    const PlanetLake& k = level.lakes()[static_cast<std::size_t>(lake)];
    if (k.playa) {
        return {-1, StreamEnd::DryBasin};
    }
    if (!k.overflowing) {
        return {-1, StreamEnd::TerminalLake};
    }
    if (k.partner == planet::kNoCell || level.cells().ocean(k.partner)) {
        return {-1, StreamEnd::Sea};
    }
    return {static_cast<std::int64_t>(k.partner), StreamEnd::Lake};
}

NetworkNext nextOf(const PlanetLevel& level, std::size_t c) {
    const PlanetCells& cells = level.cells();
    if ((cells.bits[c] & PlanetCells::kLakeBit) != 0) {
        return lakeOutlet(level, cells.lakeAt(c));
    }
    const std::uint8_t r = cells.receiver(c);
    if (r == kNoNeighbor) {
        return {-1, StreamEnd::DryBasin};
    }
    const std::size_t n = cells.grid.neighbor(c, r);
    if (cells.ocean(n)) {
        return {-1, StreamEnd::Sea};
    }
    if ((cells.bits[n] & PlanetCells::kLakeBit) != 0) {
        return lakeOutlet(level, cells.lakeAt(n));
    }
    return {static_cast<std::int64_t>(n), StreamEnd::Confluence};
}

} // namespace

PlanetNetwork::PlanetNetwork(const PlanetLevel& level, const PlanetNetworkParams& params)
    : level_(&level), params_(params), land_(planet::landIndexOf(level.cells())) {
    if (level.cells().landDischarge.size() != land_->landCells()) {
        throw std::invalid_argument("PlanetNetwork: a level without the discharge of its land (flag planet_network)");
    }
    build();
}

std::size_t PlanetNetwork::slot(std::size_t cell) const {
    return land_->slot(cell);
}

bool PlanetNetwork::land(std::size_t cell) const {
    return land_->land(cell);
}

double PlanetNetwork::discharge(std::size_t cell) const {
    return land(cell) ? planet::dischargeOf(level_->cells().landDischarge[slot(cell)]) : 0.0;
}

std::int64_t PlanetNetwork::down(std::size_t cell) const {
    return land(cell) ? nextOf(*level_, cell).cell : -1;
}

StreamEnd PlanetNetwork::ownEnd(std::size_t cell) const {
    return nextOf(*level_, cell).end;
}

StreamEnd PlanetNetwork::end(std::size_t cell) const {
    return static_cast<StreamEnd>(flags_[slot(cell)] >> kEndShift);
}

std::uint8_t PlanetNetwork::order(std::size_t cell) const {
    return land(cell) ? order_[slot(cell)] : 0;
}

std::uint32_t PlanetNetwork::area(std::size_t cell) const {
    return land(cell) ? area_[slot(cell)] : 0;
}

std::uint32_t PlanetNetwork::basin(std::size_t cell) const {
    return land(cell) ? basin_[slot(cell)] : static_cast<std::uint32_t>(cell);
}

StreamKind PlanetNetwork::kind(std::size_t cell) const {
    return static_cast<StreamKind>(flags_[slot(cell)] & kKindMask);
}

bool PlanetNetwork::mainStem(std::size_t cell) const {
    return (flags_[slot(cell)] & kMainBit) != 0;
}

StreamBank PlanetNetwork::bank(std::size_t cell) const {
    return static_cast<StreamBank>((flags_[slot(cell)] >> kBankShift) & 0x03);
}

bool PlanetNetwork::trunk(std::size_t cell) const {
    return land(cell) &&
           (area(cell) >= params_.trunkCells || discharge(cell) >= params_.trunkDischarge);
}

std::uint8_t PlanetNetwork::seed(std::size_t cell) const {
    const PlanetCells& cells = level_->cells();
    if (!land(cell) || (cells.bits[cell] & PlanetCells::kLakeBit) != 0) {
        return 0;
    }
    const float temperature = level_->temperature(cell);
    const float elevation = level_->elevation(cell);
    RunoffSite site;
    site.precipitation = level_->precipitation(cell);
    site.pet = static_cast<float>(
        planet::potentialEvapotranspiration(temperature, level_->params().climate));
    site.temperature = temperature;
    site.latitude = static_cast<float>(
        180.0 * std::abs(cells.grid.centerZ(cells.grid.row(cell))) / cells.grid.periodZ());
    site.permeability = 0.35f;
    // The slope of the streams of the cell, read on its altitude alone.
    site.slope = elevation > 2000.0f ? 0.4f : elevation > 800.0f ? 0.15f
                                        : elevation > 200.0f  ? 0.05f
                                                              : 0.01f;
    const double head = partitionRunoff(site, RunoffParams{}).headArea;
    const double steps = dmath::log(cells.grid.cellArea() / head) / dmath::log(kHortonArea);
    return static_cast<std::uint8_t>(std::clamp(1.0 + std::floor(steps), 1.0, 7.0));
}

std::size_t PlanetNetwork::memoryBytes() const {
    return order_.capacity() + 2 * area_.capacity() + 4 * basin_.capacity() + flags_.capacity();
}

} // namespace terrain
