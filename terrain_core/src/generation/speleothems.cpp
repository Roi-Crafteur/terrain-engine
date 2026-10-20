#include "terrain/generation/speleothems.hpp"

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

Candidate candidateOf(std::uint64_t cell, int cellX, int cellZ, const SpeleothemParams& p) {
    return {(cellX + 0.5 + (unitDraw(cell, kOffsetXDraw) - 0.5) * p.jitter) * p.spacing,
            (cellZ + 0.5 + (unitDraw(cell, kOffsetZDraw) - 0.5) * p.jitter) * p.spacing};
}

bool dryBiome(Biome biome) {
    return biome == Biome::Desert || biome == Biome::Savanna || biome == Biome::Beach;
}

// A void under a candidate: its ceiling and floor, where the carved density crosses 0.
struct Span {
    float ceiling;
    float floor;
};

// Height where the density crosses 0 between y (density a) and y - 1 (density b).
float crossing(double y, float a, float b) {
    return static_cast<float>(y - static_cast<double>(a / (a - b)));
}

// Highest void at least minSpan high under the column, below its ground, that is not open to the
// sky: the scan starts 1 m under the ground and skips the void it starts in.
std::optional<Span> highestSpan(CaveSampler& sampler, const CaveColumn& column, float minSpan) {
    const CaveParams& params = sampler.field().params();
    const double top = std::ceil(static_cast<double>(column.ground)) - 1.0;
    const double bottom = static_cast<double>(column.ground) - params.maxDepth - 2.0;
    const auto at = [&](double y) {
        const auto terrain = static_cast<float>(y - static_cast<double>(column.ground));
        return sampler.density(column, y, terrain);
    };
    float previous = at(top);
    bool openToSky = previous >= 0.0f;
    std::optional<float> ceiling;
    for (double y = top - 1.0; y >= bottom; y -= 1.0) {
        const float density = at(y);
        if (previous < 0.0f && density >= 0.0f) {
            ceiling = crossing(y + 1.0, previous, density);
        } else if (previous >= 0.0f && density < 0.0f) {
            const float floor = crossing(y + 1.0, previous, density);
            if (!openToSky && ceiling && *ceiling - floor >= minSpan) {
                return Span{*ceiling, floor};
            }
            openToSky = false;
            ceiling.reset();
        }
        previous = density;
    }
    return std::nullopt;
}

} // namespace

SpeleothemField::SpeleothemField(std::uint64_t seed, const SpeleothemParams& params)
    : params_(params), seed_(noise::deriveSeed(seed, kSpeleothemStream)) {}

std::vector<Speleothem> SpeleothemField::place(CaveSampler& sampler, double minX, double minZ,
                                               double maxX, double maxZ) const {
    std::vector<Speleothem> result;
    const auto first = [this](double value) {
        return static_cast<int>(std::floor(value / params_.spacing)) - 1;
    };
    const auto last = [this](double value) {
        return static_cast<int>(std::floor(value / params_.spacing)) + 1;
    };
    for (int cellZ = first(minZ); cellZ <= last(maxZ); ++cellZ) {
        for (int cellX = first(minX); cellX <= last(maxX); ++cellX) {
            const Candidate c = candidateOf(cellSeed(seed_, cellX, cellZ), cellX, cellZ, params_);
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
    const std::uint64_t cell = cellSeed(seed_, cellX, cellZ);
    const auto [x, z] = candidateOf(cell, cellX, cellZ, p);
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
    if (unitDraw(cell, kChanceDraw) >= static_cast<double>(p.chance * factor)) {
        return;
    }
    const CaveColumn column = sampler.column(x, z, ground.height(x, z));
    const std::optional<Span> span = highestSpan(sampler, column, p.minSpan);
    if (!span) {
        return;
    }
    // The tips stay above the water of a lake: a floor under the water holds no stalagmite.
    const bool flooded = column.waterLevel > span->floor;
    const float free = span->ceiling - std::max(span->floor, column.waterLevel);
    const bool ice = column.frozen || environment.temperature <= p.iceTemperature;
    const auto length = [&p](double draw, float room) {
        return std::min(static_cast<float>(p.minLength + draw * (p.maxLength - p.minLength)),
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
