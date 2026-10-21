#include "terrain/generation/speleothems.hpp"

#include "cave_scan.hpp"
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace terrain {

namespace {

// Stream of the speleothems, distinct from the other layers.
constexpr std::uint64_t kSpeleothemStream = 0x5350454C; // "SPEL"

// Draws of a candidate, one stream index each.
enum Draw : std::uint64_t {
    kOffsetXDraw,
    kOffsetZDraw,
    kChanceDraw,
    kStalactiteDraw,
    kStalagmiteChanceDraw,
    kStalagmiteDraw,
};

// The two speleothems never get closer than this.
constexpr float kGap = 0.5f;

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

// Candidate of the cell (cellX, cellZ), whose draws come from the stream cell.
struct Candidate {
    double x;
    double z;
};

// Candidate of the cell (cellX, cellZ), an unwrapped index: drawn from its canonical cell, whose
// stream cell receives, at the image of its canonical position next to the cell.
Candidate candidateOf(std::uint64_t seed, int cellX, int cellZ, const PeriodicCells& cellsX,
                      const PeriodicCells& cellsZ, double jitter, std::uint64_t& cell) {
    const auto canonicalX = static_cast<int>(cellsX.wrap(cellX));
    const auto canonicalZ = static_cast<int>(cellsZ.wrap(cellZ));
    cell = cellSeed(seed, canonicalX, canonicalZ);
    return {(canonicalX + 0.5 + (unitDraw(cell, kOffsetXDraw) - 0.5) * jitter) * cellsX.size +
                cellsX.shift(cellX),
            (canonicalZ + 0.5 + (unitDraw(cell, kOffsetZDraw) - 0.5) * jitter) * cellsZ.size +
                cellsZ.shift(cellZ)};
}

bool dryBiome(Biome biome) {
    return biome == Biome::Desert || biome == Biome::Savanna || biome == Biome::Beach;
}

using cave_scan::Span;

} // namespace

SpeleothemField::SpeleothemField(std::uint64_t seed, const SpeleothemParams& params,
                                 const WorldSize& world)
    : params_(params), cellsX_(periodicCells(world.x, params.spacing)),
      cellsZ_(periodicCells(world.z, params.spacing)),
      seed_(noise::deriveSeed(seed, kSpeleothemStream)) {
    validate(world);
}

std::vector<Speleothem> SpeleothemField::place(CaveSampler& sampler, double minX, double minZ,
                                               double maxX, double maxZ) const {
    std::vector<Speleothem> result;
    const auto first = [](double value, const PeriodicCells& cells) {
        return static_cast<int>(cells.cellOf(value)) - 1;
    };
    const auto last = [](double value, const PeriodicCells& cells) {
        return static_cast<int>(cells.cellOf(value)) + 1;
    };
    for (int cellZ = first(minZ, cellsZ_); cellZ <= last(maxZ, cellsZ_); ++cellZ) {
        for (int cellX = first(minX, cellsX_); cellX <= last(maxX, cellsX_); ++cellX) {
            std::uint64_t cell = 0;
            const Candidate c =
                candidateOf(seed_, cellX, cellZ, cellsX_, cellsZ_, params_.jitter, cell);
            if (c.x >= minX && c.x < maxX && c.z >= minZ && c.z < maxZ) {
                placeCandidate(sampler, cellX, cellZ, result);
            }
        }
    }
    return result;
}

void SpeleothemField::placeCandidate(CaveSampler& sampler, int cellX, int cellZ,
                                     std::vector<Speleothem>& result) const {
    const SpeleothemParams& p = params_;
    std::uint64_t cell = 0;
    const auto [x, z] = candidateOf(seed_, cellX, cellZ, cellsX_, cellsZ_, p.jitter, cell);
    // No speleothem in the whole column of an entrance: neither in its tube nor under it.
    const std::vector<CaveEntrance>& entrances = sampler.entrances();
    if (std::any_of(entrances.begin(), entrances.end(), [&](const CaveEntrance& entrance) {
            return entranceColumn(entrance, x, z, p.entranceMargin);
        })) {
        return;
    }
    const CaveGround& ground = sampler.ground();
    const CaveEnvironment environment =
        ground.environment ? ground.environment(x, z) : CaveEnvironment{};
    const float humid = noise::smoothstep(p.dryHumidity, p.wetHumidity, environment.humidity);
    const float factor = dryBiome(environment.biome) ? p.dryFactor
                                                     : p.dryFactor + humid * (1.0f - p.dryFactor);
    // The cave regions: more and longer speleothems (dripstone), ice (glacier).
    const CaveProfile profile = sampler.profileAt(x, z);
    const float chance = p.chance * factor * profile[CaveProfile::kSpeleothemChance];
    if (unitDraw(cell, kChanceDraw) >= static_cast<double>(chance)) {
        return;
    }
    const CaveColumn column = sampler.column(x, z, ground.height(x, z));
    if (column.lavaLevel > kDryLevel) {
        return; // no speleothem in the lava caverns
    }
    const std::optional<Span> span = cave_scan::highestSpan(sampler, column, p.minSpan);
    if (!span) {
        return;
    }
    // The tips stay above the water of a lake or a river: a floor under the water holds no
    // stalagmite.
    const float water = sampler.field().waterLevelAt(column, static_cast<double>(span->floor));
    const bool flooded = water > span->floor;
    const float free = span->ceiling - std::max(span->floor, water);
    const bool ice =
        column.frozen || environment.temperature <= p.iceTemperature || profile.icy();
    const float maxLength = p.maxLength * profile[CaveProfile::kSpeleothemLength];
    const auto length = [&p, maxLength](double draw, float room) {
        return std::min(static_cast<float>(p.minLength + draw * (maxLength - p.minLength)),
                        p.maxShare * room);
    };
    const float stalactite = length(unitDraw(cell, kStalactiteDraw), free);
    if (stalactite < p.minLength) {
        return;
    }
    result.push_back({SpeleothemKind::Stalactite, x, z, span->ceiling, stalactite,
                      stalactite * p.radiusPerLength, ice});
    const double stalagmiteChance = p.stalagmiteChance;
    if (flooded || unitDraw(cell, kStalagmiteChanceDraw) >= stalagmiteChance) {
        return;
    }
    const float room = std::min(free, span->ceiling - span->floor - stalactite - kGap);
    const float stalagmite = length(unitDraw(cell, kStalagmiteDraw), room);
    if (stalagmite >= p.minLength) {
        result.push_back({SpeleothemKind::Stalagmite, x, z, span->floor, stalagmite,
                          stalagmite * p.radiusPerLength, ice});
    }
}

} // namespace terrain
