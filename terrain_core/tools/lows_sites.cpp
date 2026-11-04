// The water of the closed lows of a world by the climate of its planet level (lows_sites.hpp).
#include "lows_sites.hpp"

#include <algorithm>
#include <cmath>

namespace lows_tool {

namespace {

constexpr std::array<const char*, kBelts> kBeltNames = {
    "cold", "hyper-arid", "arid", "semi-arid", "sub-humid", "humid", "unknown"};
constexpr std::array<const char*, kLowWaters> kWaterNames = {"dry", "salt lake", "lake", "other"};

// The grid of the lowest point: kSide x kSide points over twice the reach.
constexpr int kSide = 65;

LowWater lowWaterOf(terrain::WaterKind water) {
    switch (water) {
    case terrain::WaterKind::None:
        return LowWater::Dry;
    case terrain::WaterKind::SaltLake:
        return LowWater::Salt;
    case terrain::WaterKind::Lake:
        return LowWater::Lake;
    default:
        return LowWater::Other;
    }
}

} // namespace

const char* beltName(Belt belt) {
    return kBeltNames[static_cast<std::size_t>(belt)];
}

const char* lowWaterName(LowWater water) {
    return kWaterNames[static_cast<std::size_t>(water)];
}

Belt beltAt(const terrain::PlanetLevel* planet, double x, double z) {
    if (planet == nullptr) {
        return Belt::Unknown;
    }
    const terrain::PlanetSample p = planet->sampleAt(x, z);
    if (p.temperature < 0.0f) {
        return Belt::Cold;
    }
    const double index =
        static_cast<double>(p.precipitation) / std::max(1.0, static_cast<double>(p.pet));
    if (index < 0.05) {
        return Belt::HyperArid;
    }
    if (index < 0.2) {
        return Belt::Arid;
    }
    if (index < 0.5) {
        return Belt::SemiArid;
    }
    return index < 0.65 ? Belt::SubHumid : Belt::Humid;
}

LowFigures measureLow(const terrain::Heightfield& field, const terrain::PlanetLevel* planet,
                      const Low& low) {
    LowFigures out;
    out.low = low;
    out.belt = beltAt(planet, low.x, low.z);
    const double step = 2.0 * low.reach / (kSide - 1);
    bool any = false;
    terrain::WaterKind water = terrain::WaterKind::None;
    for (int j = 0; j < kSide; ++j) {
        for (int i = 0; i < kSide; ++i) {
            const double x = low.x - low.reach + i * step;
            const double z = low.z - low.reach + j * step;
            const terrain::HeightSample s = field.sampleAt(x, z);
            if (s.water == terrain::WaterKind::Ocean) {
                continue;
            }
            if (!any || s.height < out.lowest) {
                any = true;
                out.lowest = s.height;
                out.lowX = x;
                out.lowZ = z;
                water = s.water;
            }
        }
    }
    out.water = any ? lowWaterOf(water) : LowWater::Other;
    return out;
}

std::vector<BeltTable> beltTables(const std::vector<LowFigures>& lows) {
    std::vector<BeltTable> out;
    for (const LowFigures& f : lows) {
        auto table = std::find_if(out.begin(), out.end(),
                                  [&](const BeltTable& t) { return t.family == f.low.family; });
        if (table == out.end()) {
            out.push_back({f.low.family, {}});
            table = out.end() - 1;
        }
        ++table->counts[static_cast<std::size_t>(f.belt)][static_cast<std::size_t>(f.water)];
    }
    return out;
}

std::vector<Low> lowsOf(const terrain::Heightfield& field) {
    std::vector<Low> out;
    for (const terrain::Depression& d : field.depressions()) {
        // The reach of the depressions (depressions.cpp): 1.6 long radii (2.6 times the mean at
        // most) and their warp.
        out.push_back({"basin 11B", d.x, d.z, 1.6 * d.radius * std::sqrt(2.6) + 9'000.0});
    }
    return out;
}

} // namespace lows_tool
