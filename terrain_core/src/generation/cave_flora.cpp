#include "terrain/generation/cave_flora.hpp"

#include "cave_scan.hpp"
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace terrain {

namespace {

// Stream of the cave flora, distinct from the other layers.
constexpr std::uint64_t kFloraStream = 0x464C4F52; // "FLOR"

// Draws of a candidate, one stream index each.
enum Draw : std::uint64_t { kOffsetXDraw, kOffsetZDraw, kChanceDraw, kKindDraw, kSizeDraw };

// Share of ferns, then mushrooms, the rest being vines.
constexpr double kFernShare = 0.45;
constexpr double kMushroomShare = 0.25;

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

struct Candidate {
    double x;
    double z;
};

Candidate candidateOf(std::uint64_t cell, int cellX, int cellZ, const CaveFloraParams& p) {
    return {(cellX + 0.5 + (unitDraw(cell, kOffsetXDraw) - 0.5) * p.jitter) * p.spacing,
            (cellZ + 0.5 + (unitDraw(cell, kOffsetZDraw) - 0.5) * p.jitter) * p.spacing};
}

CaveFloraKind kindOf(double draw) {
    if (draw < kFernShare) {
        return CaveFloraKind::Fern;
    }
    return draw < kFernShare + kMushroomShare ? CaveFloraKind::Mushroom : CaveFloraKind::Vine;
}

} // namespace

CaveFloraField::CaveFloraField(std::uint64_t seed, const CaveFloraParams& params)
    : params_(params), seed_(noise::deriveSeed(seed, kFloraStream)) {}

std::vector<CaveFlora> CaveFloraField::place(CaveSampler& sampler, double minX, double minZ,
                                             double maxX, double maxZ) const {
    std::vector<CaveFlora> result;
    if (sampler.field().districts() == nullptr) {
        return result;
    }
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

void CaveFloraField::placeCandidate(CaveSampler& sampler, int cellX, int cellZ,
                                    std::vector<CaveFlora>& result) const {
    const CaveFloraParams& p = params_;
    const std::uint64_t cell = cellSeed(seed_, cellX, cellZ);
    const auto [x, z] = candidateOf(cell, cellX, cellZ, p);
    const CaveProfile profile = sampler.profileAt(x, z);
    const float lush = profile[CaveProfile::kLush];
    if (lush <= 0.0f || profile.icy() ||
        unitDraw(cell, kChanceDraw) >= static_cast<double>(p.chance * lush)) {
        return;
    }
    const std::vector<CaveEntrance>& entrances = sampler.entrances();
    if (std::any_of(entrances.begin(), entrances.end(), [&](const CaveEntrance& entrance) {
            return entranceColumn(entrance, x, z, p.entranceMargin);
        })) {
        return;
    }
    const CaveColumn column = sampler.column(x, z, sampler.ground().height(x, z));
    const std::optional<cave_scan::Span> span =
        cave_scan::highestSpan(sampler, column, p.minSpan);
    if (!span || column.frozen) {
        return;
    }
    // A floor under the water only holds vines hanging above the lake.
    const bool flooded = column.waterLevel > span->floor;
    const float free = span->ceiling - std::max(span->floor, column.waterLevel);
    CaveFloraKind kind = kindOf(unitDraw(cell, kKindDraw));
    if (flooded) {
        kind = CaveFloraKind::Vine;
    }
    const auto drawn = static_cast<float>(p.minSize + unitDraw(cell, kSizeDraw) *
                                                          (p.maxSize - p.minSize));
    const float size = std::min(drawn, p.maxShare * free);
    if (size < p.minSize) {
        return;
    }
    const float y = kind == CaveFloraKind::Vine ? span->ceiling : span->floor;
    result.push_back({kind, x, z, y, size});
}

} // namespace terrain
