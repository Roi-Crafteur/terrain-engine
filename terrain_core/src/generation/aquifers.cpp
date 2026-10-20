#include "terrain/generation/aquifers.hpp"

#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Draws of a cell, one stream index each.
enum Draw : std::uint64_t { kOffsetXDraw, kOffsetZDraw, kWetDraw, kDepthDraw };

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

std::uint64_t cellKey(int cellX, int cellZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
           static_cast<std::uint32_t>(cellZ);
}

int cellOf(double meters, double cellSize) {
    return static_cast<int>(std::floor(meters / cellSize));
}

} // namespace

AquiferCell CaveField::aquifer(int cellX, int cellZ, const CaveGround& ground) const {
    const AquiferParams& p = params_.aquifers;
    const std::uint64_t cell = cellSeed(aquiferSeed_, cellX, cellZ);
    AquiferCell result;
    result.cellX = cellX;
    result.cellZ = cellZ;
    result.siteX = (cellX + 0.5 + (unitDraw(cell, kOffsetXDraw) - 0.5) * p.jitter) * p.cellSize;
    result.siteZ = (cellZ + 0.5 + (unitDraw(cell, kOffsetZDraw) - 0.5) * p.jitter) * p.cellSize;
    const float site = ground.height(result.siteX, result.siteZ);
    float lowest = site;
    for (int probe = 0; probe < 9; ++probe) {
        const double x = (cellX + 0.5 * (probe % 3)) * p.cellSize;
        const double z = (cellZ + 0.5 * (probe / 3)) * p.cellSize;
        lowest = std::min(lowest, ground.height(x, z));
    }
    const CaveEnvironment environment =
        ground.environment ? ground.environment(result.siteX, result.siteZ) : CaveEnvironment{};
    const float humid = noise::smoothstep(p.dryHumidity, p.wetHumidity, environment.humidity);
    const float chance =
        site < kSeaLevel ? 1.0f : p.dryChance + humid * (p.wetChance - p.dryChance);
    result.wet = unitDraw(cell, kWetDraw) < static_cast<double>(chance);
    if (result.wet) {
        const double depth = p.minLevelDepth +
                             unitDraw(cell, kDepthDraw) * (p.maxLevelDepth - p.minLevelDepth);
        result.level = static_cast<float>(
            std::floor(static_cast<double>(lowest) - params_.minRoof - depth) + 0.5);
        result.frozen = environment.temperature <= p.freezeTemperature;
    }
    return result;
}

void CaveField::addAquifer(CaveColumn& column, AquiferCache& aquifers) const {
    const AquiferParams& p = params_.aquifers;
    const int baseX = cellOf(column.x, p.cellSize);
    const int baseZ = cellOf(column.z, p.cellSize);
    std::array<const AquiferCell*, 9> cells{};
    std::size_t own = 0;
    double nearest = 0.0;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& cell = aquifers.at(baseX + static_cast<int>(n % 3) - 1,
                                              baseZ + static_cast<int>(n / 3) - 1);
        cells[n] = &cell;
        const double d2 = std::pow(column.x - cell.siteX, 2) + std::pow(column.z - cell.siteZ, 2);
        if (n == 0 || d2 < nearest) {
            nearest = d2;
            own = n;
        }
    }
    const AquiferCell& home = *cells[own];
    column.waterLevel = home.wet ? home.level : kDryLevel;
    column.frozen = home.wet && home.frozen;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& other = *cells[n];
        if (n == own || (!home.wet && !other.wet)) {
            continue;
        }
        // Distance to the bisector of the two sites, on the side of the own site.
        const double far =
            std::pow(column.x - other.siteX, 2) + std::pow(column.z - other.siteZ, 2);
        const double span = std::hypot(other.siteX - home.siteX, other.siteZ - home.siteZ);
        const auto distance = static_cast<float>((far - nearest) / (2.0 * span));
        if (distance < p.barrierHalfWidth && column.barrierCount < kMaxColumnBarriers) {
            column.barriers[static_cast<std::size_t>(column.barrierCount)] = {
                p.barrierHalfWidth - distance, std::max(home.level, other.level)};
            ++column.barrierCount;
        }
    }
}

float CaveField::barrierVoid(const CaveColumn& column, double y) const {
    float result = -params_.voidClamp;
    for (int i = 0; i < column.barrierCount; ++i) {
        const AquiferBarrier& barrier = column.barriers[static_cast<std::size_t>(i)];
        const auto below = static_cast<float>(static_cast<double>(barrier.top) - y);
        result = std::max(result, std::min(barrier.width, below));
    }
    return result;
}

const AquiferCell& AquiferCache::at(int cellX, int cellZ) {
    const auto [it, inserted] = cells_.try_emplace(cellKey(cellX, cellZ));
    if (inserted) {
        it->second = field_->aquifer(cellX, cellZ, *ground_);
    }
    return it->second;
}

const AquiferCell& AquiferCache::nearest(double x, double z) {
    const double size = field_->params().aquifers.cellSize;
    const int baseX = cellOf(x, size);
    const int baseZ = cellOf(z, size);
    const AquiferCell* best = nullptr;
    double nearest = 0.0;
    for (int n = 0; n < 9; ++n) {
        const AquiferCell& cell = at(baseX + n % 3 - 1, baseZ + n / 3 - 1);
        const double d2 = std::pow(x - cell.siteX, 2) + std::pow(z - cell.siteZ, 2);
        if (best == nullptr || d2 < nearest) {
            nearest = d2;
            best = &cell;
        }
    }
    return *best;
}

float AquiferCache::highestLevelNear(double x, double z, double reach) {
    const AquiferParams& p = field_->params().aquifers;
    const int baseX = cellOf(x, p.cellSize);
    const int baseZ = cellOf(z, p.cellSize);
    std::array<const AquiferCell*, 9> cells{};
    std::size_t own = 0;
    double nearest = 0.0;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        cells[n] = &at(baseX + static_cast<int>(n % 3) - 1, baseZ + static_cast<int>(n / 3) - 1);
        const double d2 = std::pow(x - cells[n]->siteX, 2) + std::pow(z - cells[n]->siteZ, 2);
        if (n == 0 || d2 < nearest) {
            nearest = d2;
            own = n;
        }
    }
    const AquiferCell& home = *cells[own];
    float highest = home.wet ? home.level : kDryLevel;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& other = *cells[n];
        if (n == own || !other.wet) {
            continue;
        }
        const double far = std::pow(x - other.siteX, 2) + std::pow(z - other.siteZ, 2);
        const double span = std::hypot(other.siteX - home.siteX, other.siteZ - home.siteZ);
        if ((far - nearest) / (2.0 * span) < reach + p.barrierHalfWidth) {
            highest = std::max(highest, other.level);
        }
    }
    return highest;
}

} // namespace terrain
