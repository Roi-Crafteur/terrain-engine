#include "terrain/generation/cave_regions.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terrain {

namespace {

// Stream of the districts, distinct from the other layers.
constexpr std::uint64_t kDistrictStream = 0x44495354; // "DIST"

// Draws of a district, one stream index each.
enum Draw : std::uint64_t { kOffsetXDraw, kOffsetZDraw, kTypeDraw };

// Largest blend for which the 3 x 3 cells around a point hold every district weighing on it,
// as a share of the cell: a site outside them is more than (1.5 - jitter / 2) cells away, the
// nearest at most sqrt(2) (0.5 + jitter / 2) cells, and their difference must exceed the blend.
constexpr double kMaxBlendShare = 0.189;

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

using Weights = std::array<float, kCaveDistrictTypeCount>;

float& weightOf(Weights& weights, CaveDistrictType type) {
    return weights[static_cast<std::size_t>(type)];
}

// Weights of the types at a site of the given height and environment.
Weights typeWeights(float height, const CaveEnvironment& e, const CaveDistrictParams& params) {
    const bool cold = e.temperature <= 0.0f;
    const bool humid = e.humidity >= 0.6f;
    const bool mild = e.temperature >= 10.0f && e.humidity >= 0.55f;
    Weights w{};
    weightOf(w, CaveDistrictType::Tunnels) = 1.0f;
    weightOf(w, CaveDistrictType::Halls) = 1.0f;
    weightOf(w, CaveDistrictType::Crevasses) = 0.6f;
    weightOf(w, CaveDistrictType::Lakes) = humid ? 1.0f : 0.5f;
    weightOf(w, CaveDistrictType::Rivers) = humid ? 0.8f : 0.4f;
    weightOf(w, CaveDistrictType::Flooded) = height < params.floodedMaxHeight ? 0.6f : 0.0f;
    weightOf(w, CaveDistrictType::Volcanic) = cold ? 0.0f : 0.25f;
    weightOf(w, CaveDistrictType::Glacier) = cold ? 2.0f : 0.0f;
    weightOf(w, CaveDistrictType::Lush) = mild ? 1.5f : 0.0f;
    weightOf(w, CaveDistrictType::Dripstone) = e.humidity >= 0.45f ? 0.9f : 0.2f;
    return w;
}

// Type drawn with the weights: the first type whose running sum passes draw * total, never a
// type of weight 0; Tunnels if every weight is 0.
CaveDistrictType drawType(const Weights& weights, double draw) {
    double total = 0.0;
    for (const float w : weights) {
        total += w;
    }
    double target = draw * total;
    for (std::size_t t = 0; t < weights.size(); ++t) {
        if (weights[t] <= 0.0f) {
            continue;
        }
        if (target < weights[t]) {
            return static_cast<CaveDistrictType>(t);
        }
        target -= weights[t];
    }
    for (std::size_t t = weights.size(); t-- > 0;) {
        if (weights[t] > 0.0f) {
            return static_cast<CaveDistrictType>(t); // rounding at the very end of the sum
        }
    }
    return CaveDistrictType::Tunnels;
}

int cellOf(double meters, double cellSize) {
    return static_cast<int>(std::floor(meters / cellSize));
}

} // namespace

const char* caveDistrictName(CaveDistrictType type) {
    switch (type) {
    case CaveDistrictType::Tunnels:
        return "tunnels";
    case CaveDistrictType::Halls:
        return "halls";
    case CaveDistrictType::Crevasses:
        return "crevasses";
    case CaveDistrictType::Lakes:
        return "lakes";
    case CaveDistrictType::Rivers:
        return "rivers";
    case CaveDistrictType::Flooded:
        return "flooded";
    case CaveDistrictType::Volcanic:
        return "volcanic";
    case CaveDistrictType::Glacier:
        return "glacier";
    case CaveDistrictType::Lush:
        return "lush";
    case CaveDistrictType::Dripstone:
        return "dripstone";
    }
    return "";
}

std::array<CaveProfile, kCaveDistrictTypeCount> defaultDistrictProfiles() {
    std::array<CaveProfile, kCaveDistrictTypeCount> p{};
    const auto at = [&p](CaveDistrictType type) -> CaveProfile& {
        return p[static_cast<std::size_t>(type)];
    };
    using F = CaveProfile::Field;
    at(CaveDistrictType::Tunnels)[F::kSpaghettiScale] = 1.35f;
    at(CaveDistrictType::Tunnels)[F::kNoodleOffset] = -0.12f;
    at(CaveDistrictType::Tunnels)[F::kCheeseOffset] = 0.08f;
    at(CaveDistrictType::Tunnels)[F::kGalleries] = 1.0f;
    at(CaveDistrictType::Halls)[F::kSpaghettiScale] = 0.8f;
    at(CaveDistrictType::Halls)[F::kNoodleOffset] = 0.1f;
    at(CaveDistrictType::Halls)[F::kCheeseOffset] = -0.12f;
    at(CaveDistrictType::Halls)[F::kGalleries] = 0.5f;
    at(CaveDistrictType::Crevasses)[F::kRavineBoost] = 1.0f;
    at(CaveDistrictType::Crevasses)[F::kSpaghettiScale] = 0.9f;
    at(CaveDistrictType::Lakes)[F::kWetOffset] = 0.4f;
    at(CaveDistrictType::Lakes)[F::kLevelRise] = 14.0f;
    at(CaveDistrictType::Lakes)[F::kCheeseOffset] = -0.05f;
    at(CaveDistrictType::Rivers)[F::kSpaghettiScale] = 1.15f;
    at(CaveDistrictType::Rivers)[F::kWetOffset] = 0.2f;
    at(CaveDistrictType::Rivers)[F::kGalleries] = 1.0f;
    at(CaveDistrictType::Rivers)[F::kRiver] = 1.0f;
    at(CaveDistrictType::Flooded)[F::kFlooded] = 1.0f;
    at(CaveDistrictType::Flooded)[F::kCheeseOffset] = -0.08f;
    at(CaveDistrictType::Volcanic)[F::kVolcanic] = 1.0f;
    at(CaveDistrictType::Volcanic)[F::kWetOffset] = -0.5f;
    at(CaveDistrictType::Volcanic)[F::kCheeseOffset] = -0.04f;
    at(CaveDistrictType::Volcanic)[F::kGalleries] = 0.5f;
    at(CaveDistrictType::Volcanic)[F::kLava] = 1.0f;
    at(CaveDistrictType::Glacier)[F::kIce] = 1.0f;
    at(CaveDistrictType::Lush)[F::kLush] = 1.0f;
    at(CaveDistrictType::Lush)[F::kWetOffset] = 0.25f;
    at(CaveDistrictType::Lush)[F::kSpeleothemChance] = 1.2f;
    at(CaveDistrictType::Dripstone)[F::kSpeleothemChance] = 3.0f;
    at(CaveDistrictType::Dripstone)[F::kSpeleothemLength] = 1.5f;
    at(CaveDistrictType::Dripstone)[F::kCheeseOffset] = -0.04f;
    return p;
}

CaveDistricts::CaveDistricts(std::uint64_t seed, const CaveDistrictParams& params)
    : params_(params), seed_(noise::deriveSeed(seed, kDistrictStream)) {
    if (params.jitter < 0.0 || params.jitter > 0.5 || params.blend <= 0.0 ||
        params.blend > kMaxBlendShare * params.cellSize) {
        throw std::invalid_argument("cave districts: jitter in [0, 0.5] and blend in (0, "
                                    "0.189 cell] required");
    }
}

CaveDistrict CaveDistricts::district(int cellX, int cellZ, const DistrictSite& site) const {
    const std::uint64_t cell = cellSeed(seed_, cellX, cellZ);
    CaveDistrict result;
    result.cellX = cellX;
    result.cellZ = cellZ;
    const double size = params_.cellSize;
    result.siteX = (cellX + 0.5 + (unitDraw(cell, kOffsetXDraw) - 0.5) * params_.jitter) * size;
    result.siteZ = (cellZ + 0.5 + (unitDraw(cell, kOffsetZDraw) - 0.5) * params_.jitter) * size;
    float height = 0.0f;
    CaveEnvironment environment;
    site(result.siteX, result.siteZ, height, environment);
    result.type = drawType(typeWeights(height, environment, params_), unitDraw(cell, kTypeDraw));
    return result;
}

DistrictWeights CaveDistricts::weights(
    double x, double z, const std::function<const CaveDistrict&(int, int)>& districtAt) const {
    const int baseX = cellOf(x, params_.cellSize);
    const int baseZ = cellOf(z, params_.cellSize);
    std::array<const CaveDistrict*, 9> around{};
    std::array<double, 9> distance{};
    std::size_t nearest = 0;
    for (std::size_t n = 0; n < around.size(); ++n) {
        around[n] = &districtAt(baseX + static_cast<int>(n % 3) - 1,
                                baseZ + static_cast<int>(n / 3) - 1);
        distance[n] = dmath::hypot(x - around[n]->siteX, z - around[n]->siteZ);
        nearest = distance[n] < distance[nearest] ? n : nearest;
    }
    DistrictWeights result;
    result.districts[0] = *around[nearest];
    result.weights[0] = 1.0f;
    result.count = 1;
    double total = 1.0;
    const auto blend = static_cast<float>(params_.blend);
    for (std::size_t n = 0; n < around.size(); ++n) {
        const auto excess = static_cast<float>(distance[n] - distance[nearest]);
        const float w = 1.0f - noise::smoothstep(0.0f, blend, excess);
        if (n == nearest || w <= 0.0f) {
            continue;
        }
        const auto slot = static_cast<std::size_t>(result.count++);
        result.districts[slot] = *around[n];
        result.weights[slot] = w;
        total += w;
    }
    for (int i = 0; i < result.count; ++i) {
        result.weights[static_cast<std::size_t>(i)] /= static_cast<float>(total);
    }
    return result;
}

CaveProfile CaveDistricts::profile(const DistrictWeights& weights) const {
    const auto profileOf = [this](const CaveDistrict& district) -> const CaveProfile& {
        return params_.profiles[static_cast<std::size_t>(district.type)];
    };
    if (weights.count == 1) {
        return profileOf(weights.districts[0]);
    }
    // The first profile plus the weighted differences to it: the float weights only sum to 1
    // up to rounding, and this way identical profiles blend into exactly themselves.
    const CaveProfile& first = profileOf(weights.districts[0]);
    CaveProfile result;
    for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
        const double base = first.values[f];
        double value = base;
        for (int i = 1; i < weights.count; ++i) {
            const auto slot = static_cast<std::size_t>(i);
            value += static_cast<double>(weights.weights[slot]) *
                     (profileOf(weights.districts[slot]).values[f] - base);
        }
        result.values[f] = static_cast<float>(value);
    }
    return result;
}

} // namespace terrain
