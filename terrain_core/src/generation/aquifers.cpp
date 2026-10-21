#include "terrain/generation/aquifers.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Draws of a cell, one stream index each.
enum Draw : std::uint64_t {
    kOffsetXDraw,
    kOffsetZDraw,
    kWetDraw,
    kDepthDraw,
    kLavaDraw,
    kLavaDepthDraw,
};

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

double squared(double v) {
    return v * v;
}

std::uint64_t cellKey(int cellX, int cellZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) << 32) |
           static_cast<std::uint32_t>(cellZ);
}

int cellOf(double meters, const PeriodicCells& cells) {
    return static_cast<int>(cells.cellOf(meters));
}

} // namespace

// The cell at its canonical index and site, placed at the image next to (cellX, cellZ).
AquiferCell CaveField::aquifer(int cellX, int cellZ, const CaveGround& ground,
                               AquiferCache* cells) const {
    AquiferCell result =
        canonicalAquifer(static_cast<int>(aquiferCellsX_.wrap(cellX)),
                         static_cast<int>(aquiferCellsZ_.wrap(cellZ)), ground, cells);
    result.cellX = cellX;
    result.cellZ = cellZ;
    result.siteX += aquiferCellsX_.shift(cellX);
    result.siteZ += aquiferCellsZ_.shift(cellZ);
    return result;
}

AquiferCell CaveField::canonicalAquifer(int cellX, int cellZ, const CaveGround& ground,
                                        AquiferCache* cells) const {
    const AquiferParams& p = params_.aquifers;
    const double sizeX = aquiferCellsX_.size;
    const double sizeZ = aquiferCellsZ_.size;
    const std::uint64_t cell = cellSeed(aquiferSeed_, cellX, cellZ);
    AquiferCell result;
    result.siteX = (cellX + 0.5 + (unitDraw(cell, kOffsetXDraw) - 0.5) * p.jitter) * sizeX;
    result.siteZ = (cellZ + 0.5 + (unitDraw(cell, kOffsetZDraw) - 0.5) * p.jitter) * sizeZ;
    const float site = ground.height(result.siteX, result.siteZ);
    float lowest = site;
    for (int probe = 0; probe < 9; ++probe) {
        const double x = (cellX + 0.5 * (probe % 3)) * sizeX;
        const double z = (cellZ + 0.5 * (probe / 3)) * sizeZ;
        lowest = std::min(lowest, ground.height(x, z));
    }
    const CaveEnvironment environment =
        ground.environment ? ground.environment(result.siteX, result.siteZ) : CaveEnvironment{};
    // The cave regions at the site: wetter (lakes, lush), always wet up to just under the roof
    // (flooded), frozen (glacier). The neutral profile changes nothing, bit for bit.
    CaveProfile profile;
    if (districts_) {
        AquiferCache alone(*this, ground);
        profile = (cells != nullptr ? cells : &alone)->profileAt(result.siteX, result.siteZ);
    }
    const float flooded = profile[CaveProfile::kFlooded];
    const float humid = noise::smoothstep(p.dryHumidity, p.wetHumidity, environment.humidity);
    float chance = site < kSeaLevel ? 1.0f : p.dryChance + humid * (p.wetChance - p.dryChance);
    chance = std::clamp(chance + profile[CaveProfile::kWetOffset], 0.0f, 1.0f);
    chance = chance + flooded * (1.0f - chance);
    result.wet = unitDraw(cell, kWetDraw) < static_cast<double>(chance);
    result.cold = environment.temperature <= p.freezeTemperature || profile.icy();
    if (result.wet) {
        double depth = p.minLevelDepth +
                       unitDraw(cell, kDepthDraw) * (p.maxLevelDepth - p.minLevelDepth);
        depth -= profile[CaveProfile::kLevelRise];
        depth += flooded * (p.floodedDepth - depth);
        result.level = static_cast<float>(
            std::floor(static_cast<double>(lowest) - params_.minRoof - depth - 0.5) + 0.5);
        result.frozen = result.cold;
    }
    const float lavaChance = p.lavaChance * profile[CaveProfile::kLava];
    if (!result.wet && lavaChance > 0.0f &&
        unitDraw(cell, kLavaDraw) < static_cast<double>(lavaChance)) {
        const double depth = p.minLavaDepth +
                             unitDraw(cell, kLavaDepthDraw) * (p.maxLavaDepth - p.minLavaDepth);
        double level = static_cast<double>(lowest) - params_.minRoof - depth;
        level = std::min(level, lavaCeiling(cellX, cellZ, ground, cells));
        result.lava = true;
        result.lavaLevel = static_cast<float>(std::floor(level - 0.5) + 0.5);
    }
    return result;
}

void CaveField::addAquifer(CaveColumn& column, AquiferCache& aquifers) const {
    const AquiferParams& p = params_.aquifers;
    const int baseX = cellOf(column.x, aquiferCellsX_);
    const int baseZ = cellOf(column.z, aquiferCellsZ_);
    std::array<const AquiferCell*, 9> cells{};
    std::size_t own = 0;
    double nearest = 0.0;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& cell = aquifers.at(baseX + static_cast<int>(n % 3) - 1,
                                              baseZ + static_cast<int>(n / 3) - 1);
        cells[n] = &cell;
        const double d2 = squared(column.x - cell.siteX) + squared(column.z - cell.siteZ);
        if (n == 0 || d2 < nearest) {
            nearest = d2;
            own = n;
        }
    }
    const AquiferCell& home = *cells[own];
    column.waterLevel = home.wet ? home.level : kDryLevel;
    column.frozen = home.wet && home.frozen;
    column.cold = home.cold;
    column.lavaLevel = home.lava ? home.lavaLevel : kDryLevel;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& other = *cells[n];
        if (n == own || (heldLevel(home) <= kDryLevel && heldLevel(other) <= kDryLevel)) {
            continue;
        }
        // Distance to the bisector of the two sites, on the side of the own site.
        const double far = squared(column.x - other.siteX) + squared(column.z - other.siteZ);
        const double span = dmath::hypot(other.siteX - home.siteX, other.siteZ - home.siteZ);
        const auto distance = static_cast<float>((far - nearest) / (2.0 * span));
        if (distance < p.barrierHalfWidth && column.barrierCount < kMaxColumnBarriers) {
            column.barriers[static_cast<std::size_t>(column.barrierCount)] = {
                p.barrierHalfWidth - distance, std::max(heldLevel(home), heldLevel(other))};
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
        it->second = field_->aquifer(cellX, cellZ, *ground_, this);
    }
    return it->second;
}

const CaveDistrict& AquiferCache::district(int cellX, int cellZ) {
    const auto [it, inserted] = districts_.try_emplace(cellKey(cellX, cellZ));
    if (inserted) {
        it->second = field_->district(cellX, cellZ, *ground_);
    }
    return it->second;
}

DistrictWeights AquiferCache::districtWeights(double x, double z) {
    const auto districtAt = [this](int cellX, int cellZ) -> const CaveDistrict& {
        return district(cellX, cellZ);
    };
    return field_->districts()->weights(x, z, districtAt);
}

CaveProfile AquiferCache::profileAt(double x, double z) {
    const CaveDistricts* districts = field_->districts();
    return districts != nullptr ? districts->profile(districtWeights(x, z)) : CaveProfile{};
}

const AquiferCell& AquiferCache::nearest(double x, double z) {
    const int baseX = cellOf(x, field_->aquiferCellsX());
    const int baseZ = cellOf(z, field_->aquiferCellsZ());
    const AquiferCell* best = nullptr;
    double nearest = 0.0;
    for (int n = 0; n < 9; ++n) {
        const AquiferCell& cell = at(baseX + n % 3 - 1, baseZ + n / 3 - 1);
        const double d2 = squared(x - cell.siteX) + squared(z - cell.siteZ);
        if (best == nullptr || d2 < nearest) {
            nearest = d2;
            best = &cell;
        }
    }
    return *best;
}

float AquiferCache::highestLevelNear(double x, double z, double reach) {
    const AquiferParams& p = field_->params().aquifers;
    const int baseX = cellOf(x, field_->aquiferCellsX());
    const int baseZ = cellOf(z, field_->aquiferCellsZ());
    std::array<const AquiferCell*, 9> cells{};
    std::size_t own = 0;
    double nearest = 0.0;
    for (std::size_t n = 0; n < cells.size(); ++n) {
        cells[n] = &at(baseX + static_cast<int>(n % 3) - 1, baseZ + static_cast<int>(n / 3) - 1);
        const double d2 = squared(x - cells[n]->siteX) + squared(z - cells[n]->siteZ);
        if (n == 0 || d2 < nearest) {
            nearest = d2;
            own = n;
        }
    }
    const AquiferCell& home = *cells[own];
    float highest = heldLevel(home);
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const AquiferCell& other = *cells[n];
        if (n == own || heldLevel(other) <= kDryLevel) {
            continue;
        }
        const double far = squared(x - other.siteX) + squared(z - other.siteZ);
        const double span = dmath::hypot(other.siteX - home.siteX, other.siteZ - home.siteZ);
        if ((far - nearest) / (2.0 * span) < reach + p.barrierHalfWidth) {
            highest = std::max(highest, heldLevel(other));
        }
    }
    return std::max(highest, riverBandTopNear(x, z, reach));
}

} // namespace terrain
